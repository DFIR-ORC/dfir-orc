#!/bin/bash
#
# Export the objects the 'rustfs' run-vmtest.ps1 scenario uploaded into a plain directory, after the
# run and before the runner hashes them.
#
# Deploy alongside start_sinks.sh, to the ssh alias in vmtest.local.psd1 (SshHost):
#   scp tests/RestAgent/vm/fetch_rustfs.sh "$SINK":/root/
#
# Why this exists at all: every other sink in this rig stores the upload as a file, so the runner can
# sha256sum it where it landed. RustFS does not - it uses the same layout as MinIO, an object being
# <bucket>/<key>/xl.meta plus part files with small ones inlined, none of which is the uploaded byte
# stream. The byte-exact verdict therefore needs the object read back through the S3 API first.
#
# Read back as the administrator, not anonymously: the bucket policy grants s3:PutObject only, so an
# anonymous GET is refused - which is the property the scenario relies on, not an obstacle to work
# around. Reading with the admin credential keeps that refusal intact.
BUCKET=orc
PREFIX=orc-upload
PORT=8452
FETCH_DIR=/root/received-rustfs

export AWS_ACCESS_KEY_ID=rustfsadmin
export AWS_SECRET_ACCESS_KEY=orc-test-not-a-secret-admin
export AWS_DEFAULT_REGION=us-east-1
export AWS_CA_BUNDLE=/root/server.crt

ADDR=$(ip -4 -o addr show scope global | awk '{print $4}' | cut -d/ -f1 | head -1)

mkdir -p "$FETCH_DIR"
rm -f "$FETCH_DIR"/* 2>/dev/null

# The runner lists and hashes "$FETCH_DIR"/*.7z, a flat glob. The fixture uploads straight under the
# prefix, so mirroring what is below it keeps the archives at the top of the export; a key with a
# further path component would land in a subdirectory and read as "sink received nothing".
aws --endpoint-url "https://$ADDR:$PORT" s3 cp --recursive --quiet \
    "s3://$BUCKET/$PREFIX/" "$FETCH_DIR/" >/dev/null 2>&1
