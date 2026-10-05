#!/bin/bash
#
# Wipe the shared upload sink directory, and the forward-proxy logs, between run-vmtest.ps1
# scenarios, so each scenario's verdict (byte-exact for a positive, "nothing received" for a
# negative, and which proxy saw the CONNECT) reflects only that run. The runner
# invokes this over ssh before launching the capsule for each scenario.
#
# Deploy alongside start_sinks.sh, to the ssh alias in vmtest.local.psd1 (SshHost):
#   scp tests/RestAgent/vm/clear_received.sh "$SINK":/root/
#
: > /root/proxy.log 2>/dev/null
: > /root/proxy_auth.log 2>/dev/null
find /root/received/up -type f -delete 2>/dev/null
mkdir -p /root/received/up
# The second destination's sink (port 8461). Wiped here too: a destinations-* scenario that found a
# previous run's copy already there would pass without the run having delivered anything.
find /root/received2/up -type f -delete 2>/dev/null
mkdir -p /root/received2/up
