#!/bin/bash
#
# Provision the Nextcloud instance the run-vmtest.ps1 'nextcloud*' scenarios upload to, on the sink
# host. This is the checked-in master copy; deploy it next to start_sinks.sh:
#
#   scp tests/RestAgent/vm/setup_nextcloud.sh "$SINK":/root/
#   ssh "$SINK" "bash /root/setup_nextcloud.sh"          # or: bash /root/setup_nextcloud.sh 34.0.4
#
# It is idempotent: re-running it re-applies the configuration and leaves an existing installation
# (and its data) alone. Pass a version to pin one, otherwise the latest release is fetched.
#
# TLS: Nextcloud is served on port 8448 with the SAME /root/server.crt + server.key the python sinks
# use, so the committed fixtures' server_cert_pin and make-local-fixtures.ps1 apply unchanged. That
# certificate must already exist (start_sinks.sh documents how it is generated); this script does not
# create one, because a second certificate would silently break every pin in the fixture set.
#
# The credentials below are the ones the committed fixtures carry (tests/RestAgent/vm-nextcloud*.xml).
# The upload account is a plain Nextcloud user with no admin rights - the scenarios only need DAV
# write access, and the fixtures embed the password in clear, as a real deployment would.
#
# Nextcloud's password policy refuses a short password, which is why this one is not the 'secret' the
# other fixtures use.
set -e

VERSION="${1:-}"
PORT=8448
ADMIN_USER=ncadmin
ADMIN_PASSWORD=orc-test-not-a-secret-admin
UPLOAD_USER=orc
UPLOAD_PASSWORD=orc-test-not-a-secret
SHARE_FOLDER=orc-share
SHARE_TOKEN=orcvmshare00000
SHARE_PASSWORD=orc-share-not-a-secret
WWW_ROOT=/var/www/nextcloud
DATA_DIR=/var/nextcloud-data
CERT=/root/server.crt
KEY=/root/server.key

if [ ! -f "$CERT" ] || [ ! -f "$KEY" ]; then
    echo "error: $CERT / $KEY are missing - see start_sinks.sh for how the sink certificate is generated" >&2
    exit 1
fi

ADDR=$(ip -4 -o addr show scope global | awk '{print $4}' | cut -d/ -f1 | head -1)
if [ -z "$ADDR" ]; then
    echo "error: no global IPv4 address on this host" >&2
    exit 1
fi

echo "== installing packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq apache2 libapache2-mod-php php-cli php-sqlite3 php-xml php-gd php-curl \
    php-mbstring php-zip php-intl php-bcmath php-gmp unzip

echo "== fetching nextcloud"
if [ ! -d "$WWW_ROOT" ]; then
    if [ -n "$VERSION" ]; then
        URL="https://download.nextcloud.com/server/releases/nextcloud-$VERSION.zip"
    else
        URL="https://download.nextcloud.com/server/releases/latest.zip"
    fi

    rm -f /tmp/nextcloud.zip
    wget -q -O /tmp/nextcloud.zip "$URL"
    unzip -q /tmp/nextcloud.zip -d /var/www
    rm -f /tmp/nextcloud.zip
fi

mkdir -p "$DATA_DIR"
chown -R www-data:www-data "$WWW_ROOT" "$DATA_DIR"

occ() {
    sudo -u www-data php "$WWW_ROOT/occ" "$@"
}

echo "== installing the instance"
if ! occ status 2>/dev/null | grep -q "installed: true"; then
    occ maintenance:install \
        --database sqlite \
        --admin-user "$ADMIN_USER" --admin-pass "$ADMIN_PASSWORD" \
        --data-dir "$DATA_DIR"
else
    # maintenance:install runs once, so on an existing instance the admin password is whatever an
    # earlier run set. Reset it, or the summary this script prints names a password that does not work.
    sudo -u www-data env OC_PASS="$ADMIN_PASSWORD" php "$WWW_ROOT/occ" \
        user:resetpassword --password-from-env "$ADMIN_USER"
fi

echo "== configuring"
# The fixtures reach this host by IP, which Nextcloud refuses unless it is a trusted domain.
occ config:system:set trusted_domains 1 --value="$ADDR"
occ config:system:set overwrite.cli.url --value="https://$ADDR:$PORT"
occ config:system:set overwriteprotocol --value=https

# Scenarios run back to back with the same credential; the throttle would turn a later run's 200 into
# a 429 and the failure would look like an upload bug.
occ config:system:set auth.bruteforce.protection.enabled --value=false --type=boolean
occ config:system:set ratelimit.protection.enabled --value=false --type=boolean

# The upload account. sudo resets the environment, so OC_PASS has to be set on the far side of it.
#
# The password is reset on every run, not only at creation: the fixtures carry it literally, so this
# script is what makes the bench agree with them. An account left over from a run at another password
# would otherwise answer 401 and the failure would look like an upload bug.
if ! occ user:info "$UPLOAD_USER" >/dev/null 2>&1; then
    sudo -u www-data env OC_PASS="$UPLOAD_PASSWORD" php "$WWW_ROOT/occ" \
        user:add --password-from-env --display-name "ORC upload" "$UPLOAD_USER"
else
    sudo -u www-data env OC_PASS="$UPLOAD_PASSWORD" php "$WWW_ROOT/occ" \
        user:resetpassword --password-from-env "$UPLOAD_USER"
fi

echo "== configuring apache"
a2enmod ssl rewrite headers env dir mime >/dev/null

if ! grep -q "^Listen $PORT" /etc/apache2/ports.conf; then
    echo "Listen $PORT" >> /etc/apache2/ports.conf
fi

cat > /etc/apache2/sites-available/nextcloud.conf <<EOF
<VirtualHost *:$PORT>
    DocumentRoot $WWW_ROOT

    SSLEngine on
    SSLCertificateFile $CERT
    SSLCertificateKeyFile $KEY

    <Directory $WWW_ROOT/>
        Require all granted
        AllowOverride All
        Options FollowSymLinks MultiViews
    </Directory>

    ErrorLog \${APACHE_LOG_DIR}/nextcloud_error.log
    CustomLog \${APACHE_LOG_DIR}/nextcloud_access.log combined
</VirtualHost>
EOF

a2ensite nextcloud >/dev/null

# An upload is one request with the whole archive in it, so the defaults (2M) reject everything the
# scenarios send. The chunked scenario stays well under these, but the plain-PUT one does not.
PHP_INI=$(php -r 'echo php_ini_loaded_file();' | sed 's#/cli/#/apache2/#')
if [ -f "$PHP_INI" ]; then
    sed -i 's/^\s*upload_max_filesize\s*=.*/upload_max_filesize = 2G/' "$PHP_INI"
    sed -i 's/^\s*post_max_size\s*=.*/post_max_size = 2G/' "$PHP_INI"
    sed -i 's/^\s*memory_limit\s*=.*/memory_limit = 512M/' "$PHP_INI"
    sed -i 's/^\s*max_execution_time\s*=.*/max_execution_time = 600/' "$PHP_INI"
fi

systemctl restart apache2

# After apache is serving: the share is provisioned over HTTP, so it cannot run before this.
echo "== provisioning the public share"

# The password-protected public share the 'nextcloud-form' scenario uploads into, with a FIXED token.
#
# Why the token is forced rather than left as Nextcloud generated it: a <rest> step's 'path' is not
# environment-expanded (only <upload>'s own server/uri/proxy/proxy_bypass are), so the committed fixture
# has to carry the token literally, and a token Nextcloud chose at random could never be written down.
# Its share API does not let a caller pick one, so the share is created through the API and its token is
# then set in the database. That is a bench liberty; nothing in a deployment should copy it.
#
# Idempotent: the share is created once, and its password and token are re-applied on every run - the
# same reasoning as the upload account above. A share left over at another password would answer 401 and
# the failure would look like an upload bug rather than a stale bench.
provision_public_share() {
    local folder="$1" token="$2" password="$3"

    local base="https://$ADDR:$PORT"

    # The folder first: the share API refuses a path it cannot resolve, and a DAV MKCOL is the one way
    # to create it that does not need the web UI.
    python3 - "$base/remote.php/dav/files/$UPLOAD_USER/$folder/" "$UPLOAD_USER" "$UPLOAD_PASSWORD" <<'PYEOF'
import base64, ssl, sys, urllib.error, urllib.request
url, user, password = sys.argv[1:4]
req = urllib.request.Request(url, method="MKCOL")
req.add_header("Authorization", "Basic " + base64.b64encode(f"{user}:{password}".encode()).decode())
try:
    urllib.request.urlopen(req, context=ssl._create_unverified_context(), timeout=30)
    print(f"created /{url.rstrip('/').rsplit('/', 1)[1]}")
except urllib.error.HTTPError as e:
    if e.code != 405:  # 405: the collection is already there, the normal state from run two onwards
        print(f"warning: MKCOL -> {e.code}", file=sys.stderr)
PYEOF

    # Nextcloud only shares what its file cache knows about.
    sudo -u www-data php "$WWW_ROOT/occ" files:scan "$UPLOAD_USER" >/dev/null 2>&1 || true

    # Create the link share, or re-apply the password to the one already there.
    #
    # The parameters go in the QUERY STRING on purpose: this Nextcloud does not read them from a
    # urlencoded POST body and answers "Wrong path, file/folder does not exist" when they are sent that
    # way, which is a misleading enough error to be worth a comment.
    python3 - "$base/ocs/v2.php/apps/files_sharing/api/v1/shares" "$UPLOAD_USER" "$UPLOAD_PASSWORD" \
             "$folder" "$password" <<'PYEOF'
import base64, json, ssl, sys, urllib.parse, urllib.request
api, user, account_password, folder, share_password = sys.argv[1:6]
ctx = ssl._create_unverified_context()

def call(url, method="GET"):
    req = urllib.request.Request(url, method=method)
    req.add_header("Authorization", "Basic " + base64.b64encode(f"{user}:{account_password}".encode()).decode())
    req.add_header("OCS-APIRequest", "true")
    with urllib.request.urlopen(req, context=ctx, timeout=30) as r:
        return json.loads(r.read().decode())

links = [s for s in call(f"{api}?format=json&path=/{folder}")["ocs"]["data"] if s.get("share_type") == 3]
if links:
    share_id = links[0]["id"]
    query = urllib.parse.urlencode({"password": share_password, "format": "json"})
    call(f"{api}/{share_id}?{query}", method="PUT")
    print(f"public share {share_id}: password re-applied")
else:
    query = urllib.parse.urlencode({"path": f"/{folder}", "shareType": 3, "password": share_password,
                                    "permissions": 15, "format": "json"})
    print(f"public share created (id {call(f'{api}?{query}', method='POST')['ocs']['data']['id']})")
PYEOF

    php -r '
        $db = new PDO("sqlite:" . $argv[1]);
        $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
        $st = $db->prepare("UPDATE oc_share SET token = ? WHERE share_type = 3 AND file_target = ?");
        $st->execute([$argv[2], "/" . $argv[3]]);
        echo "share token pinned on ", $st->rowCount(), " row(s)\n";
    ' "$DATA_DIR/owncloud.db" "$token" "$folder"
}

provision_public_share "$SHARE_FOLDER" "$SHARE_TOKEN" "$SHARE_PASSWORD"

echo
echo "nextcloud ready on https://$ADDR:$PORT"
echo "  admin    $ADMIN_USER / $ADMIN_PASSWORD"
echo "  upload   $UPLOAD_USER / $UPLOAD_PASSWORD   (the credential the vm-nextcloud*.xml fixtures carry)"
echo "  data     $DATA_DIR/$UPLOAD_USER/files"
echo "  share    /s/$SHARE_TOKEN  password $SHARE_PASSWORD  (public share on $SHARE_FOLDER)"
