#!/bin/bash
#
# Provision the SeaweedFS instance the run-vmtest.ps1 'seaweedfs' and 'seaweedfs-sigv4' scenarios
# upload to, on the sink host. This is the checked-in master copy; deploy it next to start_sinks.sh:
#
#   scp tests/RestAgent/vm/setup_seaweedfs.sh tests/RestAgent/vm/clear_seaweedfs.sh \
#       tests/RestAgent/vm/fetch_seaweedfs.sh tests/RestAgent/vm/clear_seaweedfs_sigv4.sh \
#       tests/RestAgent/vm/fetch_seaweedfs_sigv4.sh "$SINK":/root/
#   ssh "$SINK" "bash /root/setup_seaweedfs.sh"       # or: bash /root/setup_seaweedfs.sh 4.47
#
# It is idempotent: re-running it rewrites the configuration, restarts the service and leaves existing
# buckets (and their objects) alone. Pass a release to pin another one.
#
# TLS: SeaweedFS serves S3 on port 8454 with the SAME /root/server.crt + server.key the python sinks
# use, so the committed fixtures' server_cert_pin and make-local-fixtures.ps1 apply unchanged. That
# certificate must already exist (start_sinks.sh documents how it is generated); this script does not
# create one, because a second certificate would silently break every pin in the fixture set.
#
#
# WHY A THIRD S3 SERVER
#
# MinIO and RustFS answer the question "does a real S3 server accept this dialog"; SeaweedFS answers
# it from further away. Those two are S3 clones by construction - same bucket model, the same
# xl.meta-shaped object layout, the same 'mc admin' API for identities. SeaweedFS is a distributed
# object store with an S3 GATEWAY in front of a filer, with its own identity file instead of an IAM
# API and its own on-disk representation. A signature all three accept is standard SigV4, not a
# dialect two close relatives happen to share.
#
# Like the other two it provisions BOTH shapes side by side: the anonymous bucket vm-seaweedfs.xml
# uploads to, and the separate orc-signed bucket vm-seaweedfs-sigv4.xml signs for. Neither scenario
# touches the other's bucket.
#
#
# HOW ITS ACCESS CONTROL DIFFERS FROM MINIO'S AND RUSTFS'S
#
# There is no bucket policy and no 'mc admin' here. SeaweedFS reads ONE json file of identities, each
# with credentials and a list of actions, and anonymous access is simply the identity literally named
# "anonymous" - the one with no credentials. Both grants this rig needs are a few lines of that file,
# and there is no Public Access Block to read back around (see setup_rustfs.sh), because a wildcard
# principal is not how any of it is expressed.
#
# The action syntax is "Verb:bucket/prefix/*", and the trailing '/*' is LOAD-BEARING. Measured on
# 4.47: "Write:orc-signed/orc-upload" and "Write:orc-signed/orc-upload/" both deny every PUT under
# that prefix - the object is matched with its leading separator, so only the glob form matches.
# Without it the scenario fails as an AccessDenied indistinguishable from a bad credential.
set -e

VERSION="${1:-4.47}"
PORT=8454
BUCKET=orc
PREFIX=orc-upload
DATA_DIR=/var/seaweedfs-data
CONF_DIR=/etc/seaweedfs
FETCH_DIR=/root/received-seaweedfs
CERT=/root/server.crt
KEY=/root/server.key

# 'weed server' runs a master, a volume server, a filer and the S3 gateway in ONE process, each with
# its own listen port. Two of the defaults are already taken on this bench - the volume server's 8080
# is the python http sink, the filer's 8888 is the forward proxy - so all three are moved here. Only
# $PORT is ever spoken to from outside; the other three are internal to the process group.
VOLUME_PORT=8455
FILER_PORT=8456
MASTER_PORT=9333

# The SeaweedFS administrator, used by this script and by the clear_* / fetch_* scripts only. It is
# deliberately NOT in any fixture: the anonymous scenario needs no credential, and the signed one has
# its own scoped account below.
ROOT_USER=seaweedadmin
ROOT_PASSWORD=orc-test-not-a-secret-admin

# The signed scenario's own bucket and scoped identity. This IS what vm-seaweedfs-sigv4.xml's local
# copy must carry as access_key/secret_key - a throwaway bench credential able to write one prefix of
# one bucket, never the committed fixture's problem to hide. Same values as setup_minio.sh's and
# setup_rustfs.sh's for a smaller local-fixtures diff; nothing requires them to match.
SIGNED_BUCKET=orc-signed
SIGV4_USER=orcupload
SIGV4_PASSWORD=orc-test-not-a-secret-sigv4
SIGV4_FETCH_DIR=/root/received-seaweedfs-sigv4

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
apt-get install -y -qq awscli

echo "== fetching seaweedfs ($VERSION)"
if [ ! -x /usr/local/bin/weed ]; then
    TMP=$(mktemp -d)
    trap 'rm -rf "$TMP"' EXIT
    # linux_amd64.tar.gz is a static Go binary (CGO off), so it runs on whatever this host happens to
    # be - glibc or musl - and needs no container image the way setup_minio.sh does.
    wget -q -O "$TMP/weed.tar.gz" \
        "https://github.com/seaweedfs/seaweedfs/releases/download/$VERSION/linux_amd64.tar.gz"
    tar xzf "$TMP/weed.tar.gz" -C "$TMP"
    install -m 0755 "$TMP/weed" /usr/local/bin/weed
fi

weed version | head -1

echo "== writing the s3 identities"
mkdir -p "$CONF_DIR" "$DATA_DIR" "$FETCH_DIR" "$SIGV4_FETCH_DIR"

# The whole access model of both scenarios, in one file. See "HOW ITS ACCESS CONTROL DIFFERS" above
# for why each action ends in '/*' and why "anonymous" carries no credentials block.
#
# The anonymous grant is write-only and prefix-scoped, exactly like the MinIO and RustFS bucket
# policies: writes under $PREFIX/ and nothing else - no read, no list. Anyone holding
# vm-seaweedfs.xml can write there, which is why versioning is enabled below, and why a real
# deployment of this shape must seal its output (CMS encryption) rather than trust the bucket.
cat > "$CONF_DIR/s3.json" <<EOF
{
  "identities": [
    {
      "name": "anonymous",
      "actions": ["Write:$BUCKET/$PREFIX/*"]
    },
    {
      "name": "$ROOT_USER",
      "credentials": [{ "accessKey": "$ROOT_USER", "secretKey": "$ROOT_PASSWORD" }],
      "actions": ["Admin", "Read", "List", "Tagging", "Write"]
    },
    {
      "name": "$SIGV4_USER",
      "credentials": [{ "accessKey": "$SIGV4_USER", "secretKey": "$SIGV4_PASSWORD" }],
      "actions": ["Write:$SIGNED_BUCKET/$PREFIX/*"]
    }
  ]
}
EOF
chmod 600 "$CONF_DIR/s3.json"

echo "== installing the service"
# -volume.max and -master.volumeSizeLimitMB are not tuning. SeaweedFS allocates volumes per
# COLLECTION - one collection per bucket - and pre-creates several, so the default cap of 8 is spent
# by the first bucket alone. The second then cannot get one and its PUT fails with a 500
# InternalError, "no writable volumes": a full quota that reads exactly like a broken upload. Thirty
# volumes of 1 GiB is ample for a bench holding a handful of small archives.
cat > /etc/systemd/system/seaweedfs.service <<EOF
[Unit]
Description=SeaweedFS (DFIR-ORC REST upload test sink)
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=/usr/local/bin/weed server -dir=$DATA_DIR -ip=$ADDR \\
    -master.port=$MASTER_PORT -volume.port=$VOLUME_PORT -filer -filer.port=$FILER_PORT \\
    -volume.max=30 -master.volumeSizeLimitMB=1024 \\
    -s3 -s3.port=$PORT -s3.config=$CONF_DIR/s3.json \\
    -s3.cert.file=$CERT -s3.key.file=$KEY
Restart=on-failure

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable seaweedfs >/dev/null 2>&1

# Stop, WAIT for the process to actually be gone, then start. 'weed server' takes a few seconds to
# flush its leveldb and exit; a restart that does not wait finds the old process still holding the
# ports, and what that leaves behind is a service in the failed state - every scenario then failing
# for a reason that has nothing to do with the upload.
systemctl stop seaweedfs >/dev/null 2>&1 || true
for _ in $(seq 1 30); do
    if ! pgrep -f '[w]eed server' >/dev/null 2>&1; then
        break
    fi

    sleep 1
done

systemctl start seaweedfs

export AWS_ACCESS_KEY_ID="$ROOT_USER"
export AWS_SECRET_ACCESS_KEY="$ROOT_PASSWORD"
export AWS_DEFAULT_REGION=us-east-1
export AWS_CA_BUNDLE="$CERT"
S3="aws --endpoint-url https://$ADDR:$PORT"

echo "== waiting for seaweedfs"
# A longer wait than the other two sinks need, on purpose: 'weed server' elects a raft leader before
# the S3 gateway answers anything, which takes the better part of a minute on a cold start. A bucket
# call against a server still electing fails, and the setup below would then be skipped on a run that
# otherwise looks successful.
for _ in $(seq 1 90); do
    if $S3 s3api list-buckets >/dev/null 2>&1; then
        break
    fi

    sleep 2
done

if ! $S3 s3api list-buckets >/dev/null 2>&1; then
    echo "error: seaweedfs did not answer on https://$ADDR:$PORT - see 'journalctl -u seaweedfs'" >&2
    exit 1
fi

echo "== configuring the buckets"
$S3 s3api create-bucket --bucket "$BUCKET" >/dev/null 2>&1 || true
$S3 s3api create-bucket --bucket "$SIGNED_BUCKET" >/dev/null 2>&1 || true

# An anonymous PUT replaces an object, and the fixtures upload under a guessable name. Versioning
# keeps the earlier upload as a previous version, so a clobber adds noise instead of destroying a
# collection. The clear_* scripts remove every version between runs.
$S3 s3api put-bucket-versioning --bucket "$BUCKET" \
    --versioning-configuration Status=Enabled >/dev/null
$S3 s3api put-bucket-versioning --bucket "$SIGNED_BUCKET" \
    --versioning-configuration Status=Enabled >/dev/null

# Read it back rather than trust that it applied - the same discipline setup_rustfs.sh uses on its
# bucket policy. Versioning off would not fail a run, it would quietly let one run's leftovers be
# scored as the next one's upload.
if ! $S3 s3api get-bucket-versioning --bucket "$SIGNED_BUCKET" 2>/dev/null | grep -q 'Enabled'; then
    echo "error: versioning did not apply to $SIGNED_BUCKET" >&2
    exit 1
fi

echo
echo "seaweedfs ready on https://$ADDR:$PORT"
echo "  admin    $ROOT_USER / $ROOT_PASSWORD   (awscli only - no fixture carries a SeaweedFS credential)"
echo "  bucket   $BUCKET, anonymous write on $PREFIX/ only, versioning on"
echo "  objects  s3://$BUCKET/$PREFIX/   (fetch_seaweedfs.sh exports them to $FETCH_DIR)"
echo "  sigv4    bucket $SIGNED_BUCKET, user $SIGV4_USER / $SIGV4_PASSWORD, write on $PREFIX/ only, no anonymous access"
echo "           put access_key=\"$SIGV4_USER\" secret_key=\"$SIGV4_PASSWORD\" in your LOCAL copy of"
echo "           vm-seaweedfs-sigv4.xml - never the committed fixture"
echo "  objects  s3://$SIGNED_BUCKET/$PREFIX/   (fetch_seaweedfs_sigv4.sh exports them to $SIGV4_FETCH_DIR)"
