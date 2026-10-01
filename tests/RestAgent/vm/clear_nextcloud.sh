#!/bin/bash
#
# Wipe the Nextcloud collection the 'nextcloud*' run-vmtest.ps1 scenarios upload into, between runs,
# so each scenario's verdict reflects only that run. The runner invokes this over ssh before
# launching the capsule, in place of clear_received.sh (which wipes the python sinks' directory).
#
# Deploy alongside start_sinks.sh, to the ssh alias in vmtest.local.psd1 (SshHost):
#   scp tests/RestAgent/vm/clear_nextcloud.sh "$SINK":/root/
#
# Removing the files from disk leaves Nextcloud's file cache naming files that are gone, so the scan
# below puts the cache back in step. Without it the next PUT of the same name is answered against a
# stale cache entry, and the failure looks like an upload bug rather than the leftover it is.
#
# Both collections go: orc-upload for the account scenarios, orc-share for the public-share one. The
# share itself is left alone - only its contents are wiped - because its token and password are what
# the committed fixture carries and setup_nextcloud.sh is what provisions them.
#
# The chunked scenario's upload directories are wiped too: a MOVE that never ran leaves one behind,
# and they would otherwise accumulate across runs.
WWW_ROOT=/var/www/nextcloud
DATA_DIR=/var/nextcloud-data
UPLOAD_USER=orc

rm -rf "$DATA_DIR/$UPLOAD_USER/files/orc-upload"/* 2>/dev/null
rm -rf "$DATA_DIR/$UPLOAD_USER/files/orc-share"/* 2>/dev/null
rm -rf "$DATA_DIR/$UPLOAD_USER/uploads"/* 2>/dev/null

sudo -u www-data php "$WWW_ROOT/occ" files:scan --quiet "$UPLOAD_USER" >/dev/null 2>&1
