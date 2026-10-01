#!/bin/bash
#
# Export the objects the 'seaweedfs-sigv4' run-vmtest.ps1 scenario uploaded into a plain directory,
# after the run and before the runner hashes them.
#
# Deploy alongside start_sinks.sh, to the ssh alias in vmtest.local.psd1 (SshHost):
#   scp tests/RestAgent/vm/fetch_seaweedfs_sigv4.sh "$SINK":/root/
#
# The companion of fetch_seaweedfs.sh, for the SEPARATE bucket setup_seaweedfs.sh provisions for the
# signed scenario (orc-signed, not orc). SeaweedFS keeps an object as chunks inside its volume files,
# addressed through the filer, so the byte-exact verdict needs it read back through the S3 API first -
# the same reason the anonymous bucket needs an export step.
#
# Read back with the admin credential, not the scoped orcupload one: that identity is granted writes
# only, so its own GET would be refused - which is the property the scenario relies on, not an
# obstacle to work around.
BUCKET=orc-signed
PREFIX=orc-upload
PORT=8454
FETCH_DIR=/root/received-seaweedfs-sigv4

export AWS_ACCESS_KEY_ID=seaweedadmin
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
