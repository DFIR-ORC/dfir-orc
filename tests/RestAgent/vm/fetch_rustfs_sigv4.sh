#!/bin/bash
#
# Export the objects the 'rustfs-sigv4' run-vmtest.ps1 scenario uploaded into a plain directory, after
# the run and before the runner hashes them.
#
# Deploy alongside start_sinks.sh, to the ssh alias in vmtest.local.psd1 (SshHost):
#   scp tests/RestAgent/vm/fetch_rustfs_sigv4.sh "$SINK":/root/
#
# The companion of fetch_rustfs.sh, for the SEPARATE bucket setup_rustfs.sh provisions for the signed
# scenario (orc-signed, not orc): an S3 object is <bucket>/<key>/xl.meta plus part files, never the
# uploaded byte stream, so the byte-exact verdict needs it read back through the S3 API first, same as
# the anonymous bucket.
#
# Read back with the admin credential, not the scoped orcupload one: the scoped policy grants
# s3:PutObject only, so its own GET would be refused - which is the property the scenario relies on,
# not an obstacle to work around.
BUCKET=orc-signed
PREFIX=orc-upload
PORT=8452
FETCH_DIR=/root/received-rustfs-sigv4

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
