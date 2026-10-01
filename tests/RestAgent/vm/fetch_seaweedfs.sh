#!/bin/bash
#
# Export the objects the 'seaweedfs' run-vmtest.ps1 scenario uploaded into a plain directory, after
# the run and before the runner hashes them.
#
# Deploy alongside start_sinks.sh, to the ssh alias in vmtest.local.psd1 (SshHost):
#   scp tests/RestAgent/vm/fetch_seaweedfs.sh "$SINK":/root/
#
# The counterpart of fetch_minio.sh / fetch_rustfs.sh. Those two exist because a MinIO object is
# <bucket>/<key>/xl.meta plus part files rather than the uploaded byte stream; SeaweedFS stores it
# differently again - chunks inside its own volume .dat files, addressed through the filer - and the
# conclusion is the same: there is no file on disk to hash, so the object has to be read back through
# the S3 API first.
#
# Read back with the admin credential, not anonymously: the anonymous identity is granted writes only
# (no Read, no List), which is the property the scenario relies on rather than an obstacle to work
# around.
BUCKET=orc
PREFIX=orc-upload
PORT=8454
FETCH_DIR=/root/received-seaweedfs

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
