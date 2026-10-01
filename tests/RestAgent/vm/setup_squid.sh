#!/bin/bash
#
# Configure the squid proxy the run-vmtest.ps1 'squid-*' scenarios authenticate against, on the sink
# host. This is the checked-in master copy; deploy it next to start_sinks.sh:
#
#   scp tests/RestAgent/vm/setup_squid.sh "$SINK":/root/
#   ssh "$SINK" "bash /root/setup_squid.sh basic"      # or: ntlm
#
# A squid serves ONE authentication scheme at a time, which is why vmtest.local.psd1 carries SquidMode
# and why "-Tag all" leaves the other mode's scenarios out. Re-run this script to switch mode.
#
#   basic  needs squid + apache2-utils (htpasswd). Self-contained: the account below is created here.
#   ntlm   needs a real ntlm_auth against a domain controller, which setup_addc.sh provisions. The
#          fake helper shipped with squid is NOT usable: it answers with a minimal Type-2 message that
#          real SSPI clients (WinHTTP, and Windows curl) reject, so the test would fail for a reason
#          that has nothing to do with DFIR-ORC.
#
# The credentials are the ones the committed fixtures carry (tests/RestAgent/vm-proxy-squid-*.xml):
# user 'orc', password 'secret', domain ORCTEST for the NTLM ones.
#
# Note the SSL_ports list: squid refuses CONNECT to anything else, and the sinks listen on 8443/8444,
# not on 443. Without those two ports every scenario fails at the tunnel with a squid denial.
set -e

MODE="${1:-basic}"
USER_NAME=orc
PASSWORD=secret

case "$MODE" in
    basic)
        htpasswd -bc /etc/squid/passwords "$USER_NAME" "$PASSWORD"
        chmod 640 /etc/squid/passwords
        chgrp proxy /etc/squid/passwords
        AUTH_BLOCK="auth_param basic program /usr/lib/squid/basic_ncsa_auth /etc/squid/passwords
auth_param basic children 5
auth_param basic realm orc-test
auth_param basic credentialsttl 1 minute"
        ;;
    ntlm)
        if [ ! -x /usr/bin/ntlm_auth ]; then
            echo "ntlm_auth is missing: run setup_addc.sh first" >&2
            exit 1
        fi
        AUTH_BLOCK="auth_param ntlm program /usr/bin/ntlm_auth --helper-protocol=squid-2.5-ntlmssp
auth_param ntlm children 5
auth_param ntlm keep_alive on"
        ;;
    *)
        echo "usage: $0 basic|ntlm" >&2
        exit 1
        ;;
esac

cat > /etc/squid/squid.conf <<EOF
http_port 3128

acl SSL_ports port 443 8443 8444
acl Safe_ports port 80 443 8080 8443 8444
acl CONNECT method CONNECT

$AUTH_BLOCK
acl authenticated proxy_auth REQUIRED

http_access deny !Safe_ports
http_access deny CONNECT !SSL_ports
http_access allow authenticated
http_access deny all

# Every scenario is one short-lived tunnel: keep the log unbuffered and readable, and do not spend
# time on a cache that nothing reads.
access_log stdio:/var/log/squid/access.log
cache deny all
EOF

squid -k parse
systemctl restart squid
sleep 2
systemctl is-active squid
ss -ltn | grep 3128 || { echo "squid is not listening on 3128" >&2; exit 1; }
echo "squid ready in '$MODE' mode (user $USER_NAME)"
