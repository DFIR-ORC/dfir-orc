#!/bin/bash
#
# Provision the MinIO instance the run-vmtest.ps1 'minio' and 'minio-sigv4' scenarios upload to, on
# the sink host. This is the checked-in master copy; deploy it next to start_sinks.sh:
#
#   scp tests/RestAgent/vm/setup_minio.sh tests/RestAgent/vm/clear_minio.sh \
#       tests/RestAgent/vm/fetch_minio.sh tests/RestAgent/vm/clear_minio_sigv4.sh \
#       tests/RestAgent/vm/fetch_minio_sigv4.sh "$SINK":/root/
#   ssh "$SINK" "bash /root/setup_minio.sh"        # or: bash /root/setup_minio.sh RELEASE.2025-07-23T15-54-02Z
#
# It is idempotent: re-running it re-applies the configuration and leaves an existing bucket (and its
# objects) alone. Pass a release tag to pin another one.
#
#
# WHERE THE BINARIES COME FROM
#
# Not from dl.min.io: MinIO archived the open-source server and client, and that host now answers
# 410 Gone for every community release and hotfix ("no longer maintained ... no security updates").
# The GitHub releases carry a tag and no downloadable asset. What is still published is the container
# image, which holds both /usr/bin/minio and /usr/bin/mc, so this script lifts them out of it with
# skopeo and installs them as ordinary binaries. No container runtime is involved: MinIO runs as a
# plain systemd service, reading the sink's certificate straight off the host.
#
# That also means this bench is pinned to an archived release by construction. It is a test sink, so
# an unmaintained build is acceptable where a production deployment would not be - but do not read the
# 'minio' scenario as a statement that this version is fit to deploy.
#
# TLS: MinIO is served on port 8450 with the SAME /root/server.crt + server.key the python sinks use,
# so the committed fixtures' server_cert_pin and make-local-fixtures.ps1 apply unchanged. That
# certificate must already exist (start_sinks.sh documents how it is generated); this script does not
# create one, because a second certificate would silently break every pin in the fixture set.
#
#
# WHY THE ANONYMOUS BUCKET STILL EXISTS, NOW THAT SIGNING DOES TOO
#
# This script used to be the only way to talk to MinIO at all: the REST agent had no clock and no HMAC
# among its placeholders, so it could never produce an AWS SigV4 signature, and the 'orc' bucket below
# grants anonymous s3:PutObject for exactly that reason. The agent signs its own requests now
# (authscheme="AWSv4"; see vm-minio-sigv4.xml), but the anonymous dialog is not obsolete - it is its
# own scenario ('minio', proving the unauthenticated path still works) - so this bucket, its policy and
# vm-minio.xml are UNCHANGED. The signed scenario gets its OWN bucket below ('orc-signed') instead of
# turning this one into something it was not: an anonymous bucket that also accepts a credential would
# leave the anonymous write path just as open as it is today, so removing anonymity here would prove
# nothing that a separate bucket doesn't already prove more clearly.
#
# So the policy below grants s3:PutObject - and nothing else - to anonymous callers, on the
# orc-upload/ prefix only. What that does and does not protect:
#
#   - No s3:GetObject: an anonymous GET of an uploaded object is refused. Someone holding the
#     collector's configuration cannot read back what was collected.
#   - No s3:ListBucket: they cannot enumerate object names either.
#   - They CAN write. Object keys are guessable (ORC names archives by host and timestamp) and an S3
#     PUT replaces an object, so an anonymous caller could otherwise clobber a collection. Versioning
#     is enabled below for exactly that reason: an overwrite becomes a new version and the original
#     is still there.
#
# This is a test bench, and it is also the shape a real deployment would have to take today. Treat the
# bucket as a write-only drop box on an untrusted network: seal the collection (ORC's CMS output
# encryption) so that read access to the bucket, by any route, yields sizes and timestamps rather than
# evidence.
set -e

# The last clean community release published as an image. Both binaries come out of this one image.
IMAGE_TAG="${1:-RELEASE.2025-09-07T16-13-09Z}"
IMAGE="quay.io/minio/minio:$IMAGE_TAG"
PORT=8450
CONSOLE_PORT=8451
BUCKET=orc
PREFIX=orc-upload
DATA_DIR=/var/minio-data
CERTS_DIR=/root/minio-certs
FETCH_DIR=/root/received-minio
CERT=/root/server.crt
KEY=/root/server.key
# The MinIO administrator, used by this script and by mc only. It is deliberately NOT in any fixture:
# the uploads are anonymous, so no committed file needs a MinIO credential at all.
ROOT_USER=orcminioadmin
ROOT_PASSWORD=orc-test-not-a-secret-admin
# The signed scenarios' own bucket and scoped service account (see "WHY THE ANONYMOUS BUCKET STILL
# EXISTS" below). This IS what vm-minio-sigv4.xml's local copy must carry as access_key/secret_key -
# it is a throwaway bench credential scoped to s3:PutObject on one prefix of one bucket, the same
# spirit as ROOT_PASSWORD above, never the committed fixture's problem to hide.
SIGNED_BUCKET=orc-signed
SIGV4_USER=orcupload
SIGV4_PASSWORD=orc-test-not-a-secret-sigv4
SIGV4_POLICY=orc-sigv4-put
SIGV4_FETCH_DIR=/root/received-minio-sigv4

if [ ! -f "$CERT" ] || [ ! -f "$KEY" ]; then
    echo "error: $CERT / $KEY are missing - see start_sinks.sh for how the sink certificate is generated" >&2
    exit 1
fi

ADDR=$(ip -4 -o addr show scope global | awk '{print $4}' | cut -d/ -f1 | head -1)
if [ -z "$ADDR" ]; then
    echo "error: no global IPv4 address on this host" >&2
    exit 1
fi

# Copy one file out of the pulled image into /usr/local/bin. The layers are ordinary tarballs, so the
# one holding the wanted path is found by listing them; tar detects each layer's compression itself.
install_from_image() {
    local path_in_image="$1" destination="$2" layer

    for layer in "$IMAGE_DIR"/*; do
        if tar tf "$layer" "$path_in_image" >/dev/null 2>&1; then
            tar xf "$layer" -O "$path_in_image" > "$destination"
            chmod +x "$destination"
            return 0
        fi
    done

    echo "error: $path_in_image is not in $IMAGE" >&2
    return 1
}

if [ ! -x /usr/local/bin/minio ] || [ ! -x /usr/local/bin/mc ]; then
    echo "== fetching minio ($IMAGE_TAG)"
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq skopeo

    IMAGE_DIR=$(mktemp -d)
    trap 'rm -rf "$IMAGE_DIR"' EXIT
    skopeo copy --quiet "docker://$IMAGE" "dir:$IMAGE_DIR"

    install_from_image usr/bin/minio /usr/local/bin/minio
    install_from_image usr/bin/mc /usr/local/bin/mc
fi

echo "== configuring tls"
# MinIO reads its certificate from <certs-dir>/public.crt + private.key under those exact names, so
# the sink's pair is copied rather than pointed at. Copies, not symlinks: a regenerated server.crt
# must be re-run through this script anyway, and a dangling link would fail less clearly.
mkdir -p "$CERTS_DIR" "$DATA_DIR" "$FETCH_DIR"
cp "$CERT" "$CERTS_DIR/public.crt"
cp "$KEY" "$CERTS_DIR/private.key"
chmod 600 "$CERTS_DIR/private.key"

# mc validates the certificate like any client. Trusting the sink's CA copy here keeps every mc call
# below (and fetch_minio.sh) off --insecure, so a real TLS failure still shows up as one.
mkdir -p /root/.mc/certs/CAs
cp "$CERT" /root/.mc/certs/CAs/orc-sink.crt

echo "== installing the service"
cat > /etc/systemd/system/minio.service <<EOF
[Unit]
Description=MinIO (DFIR-ORC REST upload test sink)
After=network-online.target
Wants=network-online.target

[Service]
Environment=MINIO_ROOT_USER=$ROOT_USER
Environment=MINIO_ROOT_PASSWORD=$ROOT_PASSWORD
ExecStart=/usr/local/bin/minio server $DATA_DIR --certs-dir $CERTS_DIR \\
    --address :$PORT --console-address :$CONSOLE_PORT
Restart=on-failure

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable minio >/dev/null 2>&1
systemctl restart minio

echo "== waiting for minio"
# mc alias set against a server still starting fails, and the bucket setup below would then be
# skipped on a run that otherwise looks successful.
for _ in $(seq 1 30); do
    if mc alias set local "https://$ADDR:$PORT" "$ROOT_USER" "$ROOT_PASSWORD" >/dev/null 2>&1; then
        break
    fi

    sleep 1
done

if ! mc alias set local "https://$ADDR:$PORT" "$ROOT_USER" "$ROOT_PASSWORD" >/dev/null 2>&1; then
    echo "error: minio did not answer on https://$ADDR:$PORT - see 'journalctl -u minio'" >&2
    exit 1
fi

echo "== configuring the bucket"
mc mb --ignore-existing "local/$BUCKET" >/dev/null

# An anonymous PUT replaces an object, and the fixtures upload under a guessable name. Versioning
# keeps the earlier upload as a previous version, so a clobber adds noise instead of destroying a
# collection. clear_minio.sh removes every version between runs.
mc version enable "local/$BUCKET" >/dev/null

# Write-only, and only under the one prefix: s3:PutObject alone, no s3:GetObject and no s3:ListBucket.
cat > /root/minio-anon-policy.json <<EOF
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

mc anonymous set-json /root/minio-anon-policy.json "local/$BUCKET" >/dev/null

echo "== configuring the signed bucket (AWS SigV4)"
# A SEPARATE bucket from $BUCKET above, on purpose: a scoped credential and an anonymous drop box are
# different properties (the credential is revocable and identifies who wrote), and folding the two
# together would leave nothing distinguishing the 'minio' scenario from 'minio-sigv4'. This bucket
# grants NO anonymous access at all - not even the write-only grant $BUCKET has.
mc mb --ignore-existing "local/$SIGNED_BUCKET" >/dev/null
mc version enable "local/$SIGNED_BUCKET" >/dev/null

# An IAM policy attached to one user, not a bucket policy: it names no "Principal", so it grants
# nothing to anonymous callers, unlike the policy above. s3:PutObject only, same prefix convention.
cat > /root/minio-sigv4-policy.json <<EOF
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

mc admin policy create local "$SIGV4_POLICY" /root/minio-sigv4-policy.json >/dev/null
mc admin user add local "$SIGV4_USER" "$SIGV4_PASSWORD" >/dev/null
mc admin policy attach local "$SIGV4_POLICY" --user "$SIGV4_USER" >/dev/null

# Read the attachment back rather than trust that it applied - the same discipline setup_rustfs.sh
# uses for its anonymous policy, which a Public Access Block could someday start refusing because it
# names a wildcard Principal. This policy names no Principal at all (it is attached to one identity,
# not to the bucket), so that specific block should not reach it - but asserting the read-back costs
# nothing and catches any other silent failure of 'mc admin policy attach' the same way.
if ! mc admin policy entities local --user "$SIGV4_USER" 2>/dev/null | grep -q "$SIGV4_POLICY"; then
    echo "error: $SIGV4_POLICY did not attach to $SIGV4_USER - check 'mc admin policy entities local --user $SIGV4_USER'" >&2
    exit 1
fi

mkdir -p "$SIGV4_FETCH_DIR"

echo
echo "minio ready on https://$ADDR:$PORT   (console https://$ADDR:$CONSOLE_PORT)"
echo "  admin    $ROOT_USER / $ROOT_PASSWORD   (mc only - no fixture carries a MinIO credential)"
echo "  bucket   $BUCKET, anonymous s3:PutObject on $PREFIX/ only, versioning on"
echo "  objects  s3://$BUCKET/$PREFIX/   (fetch_minio.sh exports them to $FETCH_DIR)"
echo "  sigv4    bucket $SIGNED_BUCKET, user $SIGV4_USER / $SIGV4_PASSWORD, s3:PutObject on $PREFIX/ only, no anonymous access"
echo "           put access_key=\"$SIGV4_USER\" secret_key=\"$SIGV4_PASSWORD\" in your LOCAL copy of"
echo "           vm-minio-sigv4.xml (and access_key only in vm-minio-sigv4-badkey.xml) - never the committed fixture"
echo "  objects  s3://$SIGNED_BUCKET/$PREFIX/   (fetch_minio_sigv4.sh exports them to $SIGV4_FETCH_DIR)"
