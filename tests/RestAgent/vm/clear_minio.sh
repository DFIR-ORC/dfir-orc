#!/bin/bash
#
# Wipe the MinIO prefix the 'minio' run-vmtest.ps1 scenario uploads into, between runs, so each
# scenario's verdict reflects only that run. The runner invokes this over ssh before launching the
# capsule, in place of clear_received.sh (which wipes the python sinks' directory).
#
# Deploy alongside start_sinks.sh, to the ssh alias in vmtest.local.psd1 (SshHost):
#   scp tests/RestAgent/vm/clear_minio.sh "$SINK":/root/
#
# --versions is what makes this a wipe: setup_minio.sh enables versioning on the bucket, so a plain
# remove would only add a delete marker and leave the previous run's bytes addressable underneath.
#
# The export directory fetch_minio.sh writes into is emptied too. It is what the runner hashes, and a
# leftover archive there would be scored as this run's upload - a failed upload reading as a pass.
BUCKET=orc
PREFIX=orc-upload
FETCH_DIR=/root/received-minio

mc rm --recursive --force --versions "local/$BUCKET/$PREFIX/" >/dev/null 2>&1

mkdir -p "$FETCH_DIR"
rm -f "$FETCH_DIR"/* 2>/dev/null
