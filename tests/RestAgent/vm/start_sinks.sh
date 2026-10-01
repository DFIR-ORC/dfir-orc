#!/bin/bash
#
# Start every REST-upload test sink the run-vmtest.ps1 scenarios expect, on the sink host.
#
# This is the checked-in master copy. Deploy it to your host - the runner launches DFIR-ORC on the
# Windows side and talks to these sinks over the network. $SINK is the ssh alias you put in
# tests/RestAgent/vmtest.local.psd1 (SshHost):
#
#   scp tests/RestAgent/vm/start_sinks.sh tests/RestAgent/rest_test_server.py "$SINK":/root/
#   ssh "$SINK" "bash /root/start_sinks.sh"
#
# .gitattributes keeps *.sh at LF so copying from Windows is safe. In a working copy checked out
# before that rule, these may still be CRLF - which /bin/sh rejects as a confusing "not found";
# run  sed -i 's/\r$//' /root/*.sh  on the host if that happens.
#
# It expects, in the same directory (/root): rest_test_server.py (the sink, checked in next to this
# file under tests/RestAgent/) and a server.crt/server.key pair. The certificate must be valid for
# the address the fixtures reach this host by - for an IP, an IP-SAN certificate:
#
#   openssl req -x509 -newkey rsa:2048 -nodes -days 365 \
#       -keyout /root/server.key -out /root/server.crt \
#       -subj "/CN=$ADDR" -addext "subjectAltName=IP:$ADDR"
#
# and the vm-https-*.xml fixtures must pin THAT certificate - generate re-pinned local copies with
# tests/RestAgent/make-local-fixtures.ps1 rather than editing the committed ones. The squid proxy
# (port 3128) is set up separately by setup_squid*.sh and is NOT started here.
#
# Port map (all write reassembled uploads to /root/received/up, which clear_received.sh wipes per run):
#   8080  HTTP sink
#   8443  HTTPS sink (negotiates the highest TLS both sides support); --redirect bounces any
#         /redirect/<path> to /<path> with a 307 (for the follow_redirects scenarios), and
#         /redirect-port/<port>/<path> to <path> on another port of this host
#   8444  HTTPS sink with TLS capped at 1.2 (--max-tls) for the tls-downgrade / tls-ok / os-max scenarios
#   8445  HTTPS sink with TLS capped at 1.1 (--max-tls) for the scenarios proving nothing reaches below TLS 1.2
#   8446  HTTPS sink serving ANOTHER key (server-other.crt, generated below with the same subject and SAN
#         as server.crt) for the redirect-other-key scenario: a pinned client redirected there must refuse
#   8447  HTTPS sink answering the first request for each method and path with a 401 (--challenge-once)
#         for the pin-resend scenario: every request is sent twice, and the pin is checked on both
#   8449  HTTPS sink that assembles WebDAV chunks in LEXICOGRAPHIC order (--chunk-sort lexicographic)
#         for the webdav-chunked-lex scenario: offset-named chunks then reassemble in the wrong order,
#         so the upload arrives corrupt. It proves the byte-exact check discriminates, and that the
#         numeric ordering the 8443 sink (and Nextcloud) uses is what the offset naming depends on
#   8460  HTTPS sink mocking a SERVER-RENDERED password-protected public share login (--share-login)
#         for the form-login / form-login-nocookie scenarios: GET/POST /s/<token> run the login
#         dialog, and a write under /public.php/dav/files/<token>/... needs the resulting session
#         cookie - no HTTP credential of its own. Stock Nextcloud does not serve this dialog (see
#         README "Form login"), which is why this is a mock rather than vm/setup_nextcloud.sh.
#   8888  open forward proxy
#   8889  Basic-auth forward proxy (orc:secret)
#
cd /root
pkill -f rest_test_server.py 2>/dev/null
sleep 1

# A second certificate, valid for the same address as server.crt but with its own key: what a server a
# pinned client gets redirected to looks like when it is not the pinned one. Only the key differs, so the
# pin is the only check that can refuse it.
if [ ! -f server-other.crt ] || [ ! -f server-other.key ]; then
    subject=$(openssl x509 -in server.crt -noout -subject -nameopt compat | sed 's/^subject=//')
    san=$(openssl x509 -in server.crt -noout -ext subjectAltName | tail -n +2 | sed 's/^ *//; s/IP Address:/IP:/g')
    openssl req -x509 -newkey rsa:2048 -nodes -days 365 -keyout server-other.key -out server-other.crt \
        -subj "$subject" -addext "subjectAltName=$san" 2>/dev/null
fi

setsid python3 rest_test_server.py --port 8080 --out /root/received </dev/null >/root/sink_http.log 2>&1 &
setsid python3 rest_test_server.py --port 8443 --tls --cert server.crt --key server.key --redirect --out /root/received </dev/null >/root/sink_https.log 2>&1 &
setsid python3 rest_test_server.py --port 8444 --tls --cert server.crt --key server.key --max-tls tls1.2 --out /root/received </dev/null >/root/sink_https_tls12.log 2>&1 &
setsid python3 rest_test_server.py --port 8445 --tls --cert server.crt --key server.key --max-tls tls1.1 --out /root/received </dev/null >/root/sink_https_tls11.log 2>&1 &
setsid python3 rest_test_server.py --port 8446 --tls --cert server-other.crt --key server-other.key --out /root/received </dev/null >/root/sink_https_other.log 2>&1 &
setsid python3 rest_test_server.py --port 8447 --tls --cert server.crt --key server.key --challenge-once --out /root/received </dev/null >/root/sink_https_challenge.log 2>&1 &
setsid python3 rest_test_server.py --port 8449 --tls --cert server.crt --key server.key --chunk-sort lexicographic --out /root/received </dev/null >/root/sink_https_lexchunks.log 2>&1 &
setsid python3 rest_test_server.py --port 8460 --tls --cert server.crt --key server.key --share-login orcvmshare1:orcvmshare1-secret --out /root/received </dev/null >/root/sink_https_sharelogin.log 2>&1 &
setsid python3 rest_test_server.py --proxy 8888 </dev/null >/root/proxy.log 2>&1 &
setsid python3 rest_test_server.py --proxy 8889 --proxy-auth orc:secret </dev/null >/root/proxy_auth.log 2>&1 &
sleep 2
