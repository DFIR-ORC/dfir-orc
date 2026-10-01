#!/bin/bash
#
# Provision the RustFS instance the run-vmtest.ps1 'rustfs' and 'rustfs-sigv4' scenarios upload to, on
# the sink host. This is the checked-in master copy; deploy it next to start_sinks.sh:
#
#   scp tests/RestAgent/vm/setup_rustfs.sh tests/RestAgent/vm/clear_rustfs.sh \
#       tests/RestAgent/vm/fetch_rustfs.sh tests/RestAgent/vm/clear_rustfs_sigv4.sh \
#       tests/RestAgent/vm/fetch_rustfs_sigv4.sh "$SINK":/root/
#   ssh "$SINK" "CLIENT_ADDR=<your address> bash /root/setup_rustfs.sh"   # or: ... setup_rustfs.sh 1.0.0
#
# Pass CLIENT_ADDR, the address the test client reaches this host from ('echo $SSH_CLIENT' here names
# it), or the rustfs-sigv4-port443 scenario cannot run: see 'sigv4 default port' below. The redirect it
# adds is an iptables rule, so it does not survive a reboot - re-run this script after one.
#
# It is idempotent: re-running it re-applies the configuration and leaves an existing bucket (and its
# objects) alone. Pass a release to pin another one.
#
# TLS: RustFS is served on port 8452 with the SAME /root/server.crt + server.key the python sinks use,
# so the committed fixtures' server_cert_pin and make-local-fixtures.ps1 apply unchanged. That
# certificate must already exist (start_sinks.sh documents how it is generated); this script does not
# create one, because a second certificate would silently break every pin in the fixture set.
#
#
# WHY RUSTFS SITS NEXT TO MINIO RATHER THAN REPLACING IT
#
# Both answer the same question - does a real S3 server accept this dialog - and having two answers it
# is the point: it proves the fixture is not tuned to one implementation, the way the webdav* scenarios
# do for the DAV one. MinIO covers the many deployments already in the field; RustFS covers a
# maintained target, MinIO having archived its open-source server (see setup_minio.sh).
#
# vm-rustfs.xml stays anonymous for the same reason it always was: the REST agent had no clock and no
# HMAC among its placeholders. That gap is closed now (authscheme="AWSv4"; see vm-rustfs-sigv4.xml),
# and its scoped bucket is provisioned below, alongside the unchanged anonymous one.
#
# Unlike setup_minio.sh this needs no container image for the SERVER: RustFS publishes a static
# binary, and the anonymous-bucket steps above use awscli, which Debian packages. The signed bucket
# below is the one place this script leans on 'mc admin' (RustFS answers the same admin API MinIO
# does, short of 'mc admin info' - see the README), so it expects 'mc' to already be on the host, the
# way setup_minio.sh leaves it; it does not fetch its own copy.
set -e

VERSION="${1:-1.0.0}"
PORT=8452
BUCKET=orc
PREFIX=orc-upload
DATA_DIR=/var/rustfs-data
CERTS_DIR=/root/rustfs-certs
FETCH_DIR=/root/received-rustfs
CERT=/root/server.crt
KEY=/root/server.key
# The RustFS administrator, used by this script, clear_rustfs.sh and fetch_rustfs.sh only. It is
# deliberately NOT in any fixture: the uploads are anonymous, so no committed file needs a credential.
ROOT_USER=rustfsadmin
ROOT_PASSWORD=orc-test-not-a-secret-admin
# The signed scenario's own bucket and scoped service account (see "WHY RUSTFS SITS NEXT TO MINIO"
# above). This IS what vm-rustfs-sigv4.xml's local copy must carry as access_key/secret_key - a
# throwaway bench credential scoped to s3:PutObject on one prefix of one bucket, never the committed
# fixture's problem to hide. Same values as setup_minio.sh's for a smaller local-fixtures diff; nothing
# requires them to match.
SIGNED_BUCKET=orc-signed
SIGV4_USER=orcupload
SIGV4_PASSWORD=orc-test-not-a-secret-sigv4
SIGV4_POLICY=orc-sigv4-put
SIGV4_FETCH_DIR=/root/received-rustfs-sigv4

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
apt-get install -y -qq awscli unzip

echo "== fetching rustfs ($VERSION)"
if [ ! -x /usr/local/bin/rustfs ]; then
    TMP=$(mktemp -d)
    trap 'rm -rf "$TMP"' EXIT
    # The musl build is static, so it runs on whatever this host happens to be.
    wget -q -O "$TMP/rustfs.zip" \
        "https://github.com/rustfs/rustfs/releases/download/$VERSION/rustfs-linux-x86_64-musl-v$VERSION.zip"
    unzip -o -q "$TMP/rustfs.zip" -d "$TMP"
    install -m 0755 "$TMP/rustfs" /usr/local/bin/rustfs
fi

echo "== configuring tls"
# RustFS reads its pair from <tls-path>/rustfs_cert.pem + rustfs_key.pem under those exact names, so
# the sink's pair is copied rather than pointed at. Copies, not symlinks: a regenerated server.crt
# must be re-run through this script anyway, and a dangling link would fail less clearly.
mkdir -p "$CERTS_DIR" "$DATA_DIR" "$FETCH_DIR"
cp "$CERT" "$CERTS_DIR/rustfs_cert.pem"
cp "$KEY" "$CERTS_DIR/rustfs_key.pem"
chmod 600 "$CERTS_DIR/rustfs_key.pem"

# Says outright whether the pair parsed, rather than leaving a TLS failure to surface later as a
# scenario that cannot connect.
rustfs tls inspect --path "$CERTS_DIR"

echo "== installing the service"
cat > /etc/systemd/system/rustfs.service <<EOF
[Unit]
Description=RustFS (DFIR-ORC REST upload test sink)
After=network-online.target
Wants=network-online.target

[Service]
Environment=RUSTFS_ACCESS_KEY=$ROOT_USER
Environment=RUSTFS_SECRET_KEY=$ROOT_PASSWORD
Environment=RUSTFS_TLS_PATH=$CERTS_DIR
ExecStart=/usr/local/bin/rustfs server $DATA_DIR --address :$PORT
Restart=on-failure

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable rustfs >/dev/null 2>&1
systemctl restart rustfs

export AWS_ACCESS_KEY_ID="$ROOT_USER"
export AWS_SECRET_ACCESS_KEY="$ROOT_PASSWORD"
export AWS_DEFAULT_REGION=us-east-1
export AWS_CA_BUNDLE="$CERT"
S3="aws --endpoint-url https://$ADDR:$PORT"

echo "== waiting for rustfs"
# A bucket call against a server still starting fails, and the setup below would then be skipped on a
# run that otherwise looks successful.
for _ in $(seq 1 30); do
    if $S3 s3api list-buckets >/dev/null 2>&1; then
        break
    fi

    sleep 1
done

if ! $S3 s3api list-buckets >/dev/null 2>&1; then
    echo "error: rustfs did not answer on https://$ADDR:$PORT - see 'journalctl -u rustfs'" >&2
    exit 1
fi

echo "== configuring the bucket"
$S3 s3api create-bucket --bucket "$BUCKET" >/dev/null 2>&1 || true

# An anonymous PUT replaces an object, and the fixtures upload under a guessable name. Versioning
# keeps the earlier upload as a previous version, so a clobber adds noise instead of destroying a
# collection. clear_rustfs.sh removes every version between runs.
$S3 s3api put-bucket-versioning --bucket "$BUCKET" \
    --versioning-configuration Status=Enabled >/dev/null

# Write-only, and only under the one prefix: s3:PutObject alone, no s3:GetObject and no s3:ListBucket.
cat > /root/rustfs-anon-policy.json <<EOF
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Principal": { "AWS": ["*"] },
            "Action": ["s3:PutObject"],
            "Resource": ["arn:aws:s3:::$BUCKET/$PREFIX/*"]
        }
    ]
}
EOF

$S3 s3api put-bucket-policy --bucket "$BUCKET" --policy file:///root/rustfs-anon-policy.json

# Read the policy back rather than trust that it applied. RustFS documents a Public Access Block that
# REFUSES a wildcard-principal Allow; it is off by default today, but if a release ever turns it on,
# the upload would start failing with an anonymous 403 and the cause would look like an ORC bug.
if ! $S3 s3api get-bucket-policy --bucket "$BUCKET" --output text 2>/dev/null | grep -q 's3:PutObject'; then
    echo "error: the anonymous write policy did not apply - check the bucket's Public Access Block" >&2
    exit 1
fi

echo "== configuring the signed bucket (AWS SigV4)"
if ! command -v mc >/dev/null 2>&1; then
    echo "error: 'mc' is not installed - the signed bucket is provisioned through the same admin API" >&2
    echo "       setup_minio.sh uses (see the README); run that once first, or install mc yourself" >&2
    exit 1
fi

mc alias set rustfs "https://$ADDR:$PORT" "$ROOT_USER" "$ROOT_PASSWORD" >/dev/null

# A SEPARATE bucket from $BUCKET above, on purpose: a scoped credential and an anonymous drop box are
# different properties, and folding the two together would leave nothing distinguishing the 'rustfs'
# scenario from 'rustfs-sigv4'. This bucket grants NO anonymous access at all.
mc mb --ignore-existing "rustfs/$SIGNED_BUCKET" >/dev/null
mc version enable "rustfs/$SIGNED_BUCKET" >/dev/null

# An IAM policy attached to one user, not a bucket policy: it names no "Principal", so it grants
# nothing to anonymous callers, unlike the policy above. s3:PutObject only, same prefix convention.
cat > /root/rustfs-sigv4-policy.json <<EOF
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Action": ["s3:PutObject"],
            "Resource": ["arn:aws:s3:::$SIGNED_BUCKET/$PREFIX/*"]
        }
    ]
}
EOF

mc admin policy create rustfs "$SIGV4_POLICY" /root/rustfs-sigv4-policy.json >/dev/null
mc admin user add rustfs "$SIGV4_USER" "$SIGV4_PASSWORD" >/dev/null
mc admin policy attach rustfs "$SIGV4_POLICY" --user "$SIGV4_USER" >/dev/null

# Read the attachment back rather than trust that it applied - the same discipline the anonymous
# policy above uses against RustFS's Public Access Block. This policy names no Principal at all (it is
# attached to one identity, not to the bucket), so that specific block should not reach it - but
# asserting the read-back costs nothing and catches any other silent failure the same way.
if ! mc admin policy entities rustfs --user "$SIGV4_USER" 2>/dev/null | grep -q "$SIGV4_POLICY"; then
    echo "error: $SIGV4_POLICY did not attach to $SIGV4_USER - check 'mc admin policy entities rustfs --user $SIGV4_USER'" >&2
    exit 1
fi

mkdir -p "$SIGV4_FETCH_DIR"


# sigv4 default port: make https://<addr> (port 443) reach this server, for the
# rustfs-sigv4-port443 scenario. WinHTTP omits the port from the Host header when it is the scheme
# default, so a client writing server="https://host:443" must sign "host" and not "host:443"; that is
# the only URL shape which proves the signer normalises it, and it needs something answering on 443.
#
# Apache owns *:443 on this bench (it also serves Nextcloud on 8448), so the port is NOT taken over.
# A NAT redirect is added for ONE client address instead: it is applied in PREROUTING, before the
# socket lookup, so that client reaches this server while apache keeps serving 443 to everyone else.
# Skipped when CLIENT_ADDR is unset. Remove with the same command and -D instead of -A.
if [ -n "${CLIENT_ADDR:-}" ]; then
    if iptables -t nat -C PREROUTING -s "$CLIENT_ADDR/32" -p tcp --dport 443 -j REDIRECT --to-ports "$PORT" 2>/dev/null; then
        echo "sigv4 default port: redirect 443 -> $PORT already present for $CLIENT_ADDR"
    elif iptables -t nat -A PREROUTING -s "$CLIENT_ADDR/32" -p tcp --dport 443 -j REDIRECT --to-ports "$PORT"; then
        echo "sigv4 default port: redirected 443 -> $PORT for $CLIENT_ADDR (apache still owns 443 for others)"
    else
        echo "warning: could not add the 443 redirect; the rustfs-sigv4-port443 scenario will not run" >&2
    fi
else
    echo "sigv4 default port: CLIENT_ADDR unset, skipping the 443 redirect (rustfs-sigv4-port443 needs it)"
fi
echo
echo "rustfs ready on https://$ADDR:$PORT"
echo "  admin    $ROOT_USER / $ROOT_PASSWORD   (awscli only - no fixture carries a RustFS credential)"
echo "  bucket   $BUCKET, anonymous s3:PutObject on $PREFIX/ only, versioning on"
echo "  objects  s3://$BUCKET/$PREFIX/   (fetch_rustfs.sh exports them to $FETCH_DIR)"
echo "  sigv4    bucket $SIGNED_BUCKET, user $SIGV4_USER / $SIGV4_PASSWORD, s3:PutObject on $PREFIX/ only, no anonymous access"
echo "           put access_key=\"$SIGV4_USER\" secret_key=\"$SIGV4_PASSWORD\" in your LOCAL copy of"
echo "           vm-rustfs-sigv4.xml - never the committed fixture"
echo "  objects  s3://$SIGNED_BUCKET/$PREFIX/   (fetch_rustfs_sigv4.sh exports them to $SIGV4_FETCH_DIR)"
