#!/bin/bash
#
# Wipe the RustFS prefix the 'rustfs' run-vmtest.ps1 scenario uploads into, between runs, so each
# scenario's verdict reflects only that run. The runner invokes this over ssh before launching the
# capsule, in place of clear_received.sh (which wipes the python sinks' directory).
#
# Deploy alongside start_sinks.sh, to the ssh alias in vmtest.local.psd1 (SshHost):
#   scp tests/RestAgent/vm/clear_rustfs.sh "$SINK":/root/
#
# Deleting by VERSION is what makes this a wipe: setup_rustfs.sh enables versioning on the bucket, so
# removing the key alone would only add a delete marker and leave the previous run's bytes
# addressable underneath.
#
# The export directory fetch_rustfs.sh writes into is emptied too. It is what the runner hashes, and a
# leftover archive there would be scored as this run's upload - a failed upload reading as a pass.
BUCKET=orc
PREFIX=orc-upload
PORT=8452
FETCH_DIR=/root/received-rustfs

export AWS_ACCESS_KEY_ID=rustfsadmin
export AWS_SECRET_ACCESS_KEY=orc-test-not-a-secret-admin
export AWS_DEFAULT_REGION=us-east-1
export AWS_CA_BUNDLE=/root/server.crt

ADDR=$(ip -4 -o addr show scope global | awk '{print $4}' | cut -d/ -f1 | head -1)
S3="aws --endpoint-url https://$ADDR:$PORT"

# An empty prefix yields a document with no Objects member, which delete-objects rejects - hence the
# guard rather than an unconditional call, so a clear on an already-empty bucket stays silent.
$S3 s3api list-object-versions --bucket "$BUCKET" --prefix "$PREFIX/" \
    --query '{Objects: Versions[].{Key:Key,VersionId:VersionId}}' --output json > /tmp/rustfs-clear.json 2>/dev/null

if grep -q '"Key"' /tmp/rustfs-clear.json 2>/dev/null; then
    $S3 s3api delete-objects --bucket "$BUCKET" --delete file:///tmp/rustfs-clear.json >/dev/null 2>&1
fi

rm -f /tmp/rustfs-clear.json 2>/dev/null

mkdir -p "$FETCH_DIR"
rm -f "$FETCH_DIR"/* 2>/dev/null
