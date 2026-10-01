#!/bin/bash
#
# Wipe the MinIO prefix the 'minio-sigv4' and 'minio-sigv4-badkey' run-vmtest.ps1 scenarios upload
# into, between runs, so each scenario's verdict reflects only that run. The runner invokes this over
# ssh before launching the capsule, in place of clear_received.sh (which wipes the python sinks'
# directory).
#
# Deploy alongside start_sinks.sh, to the ssh alias in vmtest.local.psd1 (SshHost):
#   scp tests/RestAgent/vm/clear_minio_sigv4.sh "$SINK":/root/
#
# The companion of clear_minio.sh, for the SEPARATE bucket setup_minio.sh provisions for the signed
# scenarios (orc-signed, not orc). --versions is what makes this a wipe: setup_minio.sh enables
# versioning on that bucket too, so a plain remove would only add a delete marker and leave the
# previous run's bytes addressable underneath.
#
# The export directory fetch_minio_sigv4.sh writes into is emptied too. It is what the runner hashes,
# and a leftover archive there would be scored as this run's upload - a failed upload reading as a pass.
BUCKET=orc-signed
PREFIX=orc-upload
FETCH_DIR=/root/received-minio-sigv4

mc rm --recursive --force --versions "local/$BUCKET/$PREFIX/" >/dev/null 2>&1

mkdir -p "$FETCH_DIR"
rm -f "$FETCH_DIR"/* 2>/dev/null
