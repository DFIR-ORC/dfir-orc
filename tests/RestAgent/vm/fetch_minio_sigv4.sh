#!/bin/bash
#
# Export the objects the 'minio-sigv4' and 'minio-sigv4-badkey' run-vmtest.ps1 scenarios uploaded into
# a plain directory, after the run and before the runner hashes them.
#
# Deploy alongside start_sinks.sh, to the ssh alias in vmtest.local.psd1 (SshHost):
#   scp tests/RestAgent/vm/fetch_minio_sigv4.sh "$SINK":/root/
#
# The companion of fetch_minio.sh, for the SEPARATE bucket setup_minio.sh provisions for the signed
# scenarios (orc-signed, not orc): an S3 object is <bucket>/<key>/xl.meta plus part files, never the
# uploaded byte stream, so the byte-exact verdict needs it read back through the S3 API first, same as
# the anonymous bucket.
#
# Read back with the admin alias, not the scoped orcupload credential: the scoped policy grants
# s3:PutObject only, so its own GET would be refused - which is the property the scenario relies on,
# not an obstacle to work around.
BUCKET=orc-signed
PREFIX=orc-upload
FETCH_DIR=/root/received-minio-sigv4

mkdir -p "$FETCH_DIR"
rm -f "$FETCH_DIR"/* 2>/dev/null

# The runner lists and hashes "$FETCH_DIR"/*.7z, a flat glob. The fixture uploads straight under the
# prefix, so mirroring what is below it keeps the archives at the top of the export; a key with a
# further path component would land in a subdirectory and read as "sink received nothing".
mc cp --quiet --recursive "local/$BUCKET/$PREFIX/" "$FETCH_DIR/" >/dev/null 2>&1
