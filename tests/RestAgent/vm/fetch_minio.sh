#!/bin/bash
#
# Export the objects the 'minio' run-vmtest.ps1 scenario uploaded into a plain directory, after the
# run and before the runner hashes them.
#
# Deploy alongside start_sinks.sh, to the ssh alias in vmtest.local.psd1 (SshHost):
#   scp tests/RestAgent/vm/fetch_minio.sh "$SINK":/root/
#
# Why this exists at all: every other sink in this rig stores the upload as a file, so the runner can
# sha256sum it where it landed. MinIO does not - an object lives as <bucket>/<key>/xl.meta plus part
# files, with small objects inlined into the metadata, none of which is the uploaded byte stream. The
# byte-exact verdict therefore needs the object read back through the S3 API first.
#
# Read back as the administrator, not anonymously: the bucket policy grants s3:PutObject only, so an
# anonymous GET is refused - which is the property the scenario relies on, not an obstacle to work
# around. Reading with mc's alias credential keeps that refusal intact.
BUCKET=orc
PREFIX=orc-upload
FETCH_DIR=/root/received-minio

mkdir -p "$FETCH_DIR"
rm -f "$FETCH_DIR"/* 2>/dev/null

# The runner lists and hashes "$FETCH_DIR"/*.7z, a flat glob. The fixture uploads straight under the
# prefix, so mirroring what is below it keeps the archives at the top of the export; a key with a
# further path component would land in a subdirectory and read as "sink received nothing".
mc cp --quiet --recursive "local/$BUCKET/$PREFIX/" "$FETCH_DIR/" >/dev/null 2>&1
