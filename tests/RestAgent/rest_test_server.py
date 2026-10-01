#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Minimal upload sink to exercise DFIR-ORC's RESTAgent. Standard library only (no pip).
#
# RESTAgent issues ONE HTTP request per chunk:
#   - method  = <upload>'s `http_method` (default PUT; PATCH is a natural fit for partial/ranged writes)
#   - URL     = RootPath + http_query, with {start} {end} {length} {total} {filename} substituted
#   - headers = each <header> template, same substitution
#   - body    = the raw file bytes for that chunk; Content-Length = chunk length
#   - success = any 2xx status
#
# This server logs exactly what each request carried (method, path, query, headers, body size)
# and reassembles the file at the byte offset it infers, so you can diff the result against the
# original. PUT/POST/PATCH write; a WebDAV verb subset (OPTIONS, PROPFIND, PROPPATCH, MKCOL,
# MOVE, COPY, DELETE, HEAD, GET, LOCK, UNLOCK) lets it stand in for a real DAV endpoint - clients
# probe with OPTIONS/PROPFIND and many do atomic uploads as PUT-to-temp then MOVE. It supports
# HTTP and HTTPS; HTTPS lets you test `https_security`, `server_cert_pin` (paste the very cert this
# server uses), and `insecure` (which also disables cert validation, in all builds).
#
# Usage:
#   python rest_test_server.py --port 8080 --out ./received
#   python rest_test_server.py --port 8443 --tls --out ./received        # HTTPS with the EMBEDDED cert
#   python rest_test_server.py --port 8443 --tls --cert server.crt --key server.key --out ./received
#   python rest_test_server.py --port 8443 --gencert --out ./received    # auto-make cert+key via openssl
#   python rest_test_server.py --port 8080 --auth orc:secret            # require Basic auth
#   python rest_test_server.py --port 8455 --tls --cert server.crt --key server.key \
#       --share-login TOKEN:PASSWORD --out ./received   # mock a SERVER-RENDERED public share login
#                                                         # (see README "Form login")
#
# --share-login serves a real HTML <form> with hidden inputs. No Nextcloud does that - stock and the
# every deployment of it script-renders the dialog and carries the CSRF token on a <head> attribute, as
# vm-nextcloud-form.xml covers against a real instance. This mode is the rig's only form-rendering
# target, and it exists so the 'form_input' capture source is exercised end to end somewhere.
#
# --tls (without --cert/--key) serves a self-signed cert EMBEDDED in this script - no openssl, no files.
# Its base64-DER is the value already pinned by `server_cert_pin` in the rest-proxy*.xml / rest-https-pin
# fixtures, so pinning works out of the box. --cert/--key override it. --gencert still generates a fresh
# cert+key via openssl (server.crt/server.key by default) if you prefer; paste that cert into
# `server_cert_pin` yourself when using it.
# Equivalent manual openssl invocation:
#   openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt \
#       -days 365 -nodes -subj "/CN=localhost" \
#       -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"

import argparse
import base64
import email.utils
import os
import posixpath
import re
import secrets
import select
import shutil
import socket
import subprocess
import sys
import threading
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs, unquote, quote
from xml.sax.saxutils import escape as xml_escape

DAV_METHODS = "OPTIONS, GET, HEAD, PUT, POST, PATCH, DELETE, PROPFIND, PROPPATCH, MKCOL, MOVE, COPY, LOCK, UNLOCK"


def parse_args():
    p = argparse.ArgumentParser(description="Minimal REST/WebDAV upload sink for testing DFIR-ORC RESTAgent")
    p.add_argument("--host", default="0.0.0.0", help="bind address (default: all interfaces)")
    p.add_argument("--port", type=int, default=8080, help="listen port (default: 8080)")
    p.add_argument("--out", default="./received", help="directory to write reassembled uploads")
    p.add_argument("--tls", action="store_true", help="serve HTTPS (requires --cert and --key)")
    p.add_argument("--cert", help="PEM certificate for --tls (default server.crt with --gencert)")
    p.add_argument("--key", help="PEM private key for --tls (default server.key with --gencert)")
    p.add_argument(
        "--max-tls",
        choices=["tls1.0", "tls1.1", "tls1.2", "tls1.3"],
        help="cap the server's MAXIMUM TLS version (with --tls); e.g. tls1.2 to test that a client "
        "requiring a higher version fails closed instead of downgrading",
    )
    p.add_argument(
        "--gencert",
        action="store_true",
        help="generate a self-signed cert+key (via openssl) if absent, then serve HTTPS; paste the cert into server_cert_pin",
    )
    p.add_argument("--openssl", help="path to the openssl executable (else PATH, else Git for Windows)")
    p.add_argument("--cn", default="localhost", help="certificate CommonName/SAN host for --gencert (default: localhost)")
    p.add_argument("--days", type=int, default=365, help="certificate validity in days for --gencert (default: 365)")
    p.add_argument("--force", action="store_true", help="with --gencert, overwrite an existing cert/key")
    p.add_argument("--auth", help="require HTTP Basic auth, given as user:password")
    p.add_argument(
        "--challenge-once",
        action="store_true",
        help="answer the first request for each method and path with a 401, and serve it when sent again, to "
        "exercise a client's resend on an auth challenge",
    )
    p.add_argument("--status", type=int, default=200, help="status code to return on a successful write (default: 200)")
    p.add_argument("--dump-body", action="store_true", help="also hex/ascii preview the first bytes of each body")
    p.add_argument(
        "--chunk-sort",
        choices=("numeric", "lexicographic"),
        default="numeric",
        help="order in which a MOVE of <dir>/.file assembles the chunk files it finds (default: numeric, as "
        "Nextcloud does; 'lexicographic' deliberately corrupts an offset-named upload, for a negative test)")
    p.add_argument(
        "--redirect",
        action="store_true",
        help="answer any /redirect/<path> request with a 307 to /<path>, to exercise a client's follow_redirects option",
    )
    p.add_argument("--proxy", type=int, metavar="PORT", help="run as a forward HTTP proxy on PORT instead of the upload sink")
    p.add_argument("--proxy-auth", metavar="USER:PASS", help="require Basic proxy auth (with --proxy); format user:pass")
    p.add_argument(
        "--share-login",
        metavar="TOKEN:PASSWORD",
        help="mock a Nextcloud FORK's password-protected public-share login: GET /s/TOKEN serves an "
        "HTML form with a fresh per-session requesttoken and a session cookie, POST /s/TOKEN validates "
        "password+requestToken+sharingToken+sharingType and authenticates the session, and a write under "
        "/public.php/dav/files/TOKEN/... is refused with 401 unless that session is authenticated. "
        "Opt-in only: without this flag the sink's behaviour is unchanged. See README 'Form login'.",
    )
    return p.parse_args()


# Embedded self-signed test certificate (CN=localhost, SAN localhost + 127.0.0.1). Lets the sink serve
# HTTPS with no openssl and no external files. Its base64-DER is the exact value pinned by
# `server_cert_pin` in rest-proxy*.xml / rest-https-pin.xml, so certificate pinning matches out of the
# box with no manual step. BEWARE: this is a throwaway TEST key checked into the repo - never reuse it
# for anything real. Pass --cert/--key to serve a different certificate.
_EMBEDDED_CERT_PEM = """\
-----BEGIN CERTIFICATE-----
MIIDJTCCAg2gAwIBAgIUdYq0185rJL/bnLJmK0yl38xHtFswDQYJKoZIhvcNAQEL
BQAwFDESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2MDYxMjE1MTEwNFoXDTI3MDYx
MjE1MTEwNFowFDESMBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEF
AAOCAQ8AMIIBCgKCAQEAtC/JJQ9K1d1WcZIpkbQBLknmsrHSvxiJcUgyVaB3o2w4
M+IkQH5qW0aTHI8nYn+dEiT+SgSWh2ARqq7xE+oSN8NDitf6j2xwRZrWdb8rgPLw
ZHdPpI+q0a06vhX9i9cRoT0MnyVgvabzZgzHUnBxB1Oig5K6iasWjpvSzz/V4YeR
Zz56CIkOtEXsFJqExiAzCjh+Jo25ysC4km53QZ6X/rbACArcoZ0cKPtH1BLNGvQP
hoSipvc862yKwp+Je6hIplt2FzCPDzHhWF+97ovnuiJWWkeWx1s4Zop6x72P0IGS
rOznxPwy0ZXhxB2TrNDvwMu2uoTgzubTsKHxQijEswIDAQABo28wbTAdBgNVHQ4E
FgQU6PfU4GSU3NXLux8q0DIpdC42eYkwHwYDVR0jBBgwFoAU6PfU4GSU3NXLux8q
0DIpdC42eYkwDwYDVR0TAQH/BAUwAwEB/zAaBgNVHREEEzARgglsb2NhbGhvc3SH
BH8AAAEwDQYJKoZIhvcNAQELBQADggEBAHm5XylKgjh/x2SL+iNTZBeiWUZgUiJ9
ZB+K+n24OML4eqldD4VModEQGuKfCvo9wqK2QZE0f6+JPRObgF8WXVIZr3YRB2QB
Xb18Qab6vV3pQxZzzVRCxBXCxRkv02goLFfuw6INPj2GWWNktoMmfTwD42oUePIs
z1BMAcZTrFu9BQIS1efvFT76Em+7KjWAfboYpVZfKkdHzB9bVc/KHVkvAylUFE+C
3SjCRJBQuHtoX+hM3RvDraEkEQwMtPGsQaAGfGqhpPgf18+g/ZBnC+uuyHVRbl1d
KEGG+kha65J4tr1MSIBxrGJ3FWPMRdYA5S0dFQhQPLN8czf8TGddC0o=
-----END CERTIFICATE-----
"""

_EMBEDDED_KEY_PEM = """\
-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQC0L8klD0rV3VZx
kimRtAEuSeaysdK/GIlxSDJVoHejbDgz4iRAfmpbRpMcjydif50SJP5KBJaHYBGq
rvET6hI3w0OK1/qPbHBFmtZ1vyuA8vBkd0+kj6rRrTq+Ff2L1xGhPQyfJWC9pvNm
DMdScHEHU6KDkrqJqxaOm9LPP9Xhh5FnPnoIiQ60RewUmoTGIDMKOH4mjbnKwLiS
bndBnpf+tsAICtyhnRwo+0fUEs0a9A+GhKKm9zzrbIrCn4l7qEimW3YXMI8PMeFY
X73ui+e6IlZaR5bHWzhminrHvY/QgZKs7OfE/DLRleHEHZOs0O/Ay7a6hODO5tOw
ofFCKMSzAgMBAAECggEAL+ZcJHvyOvRCHKLsZg1iGvoOWAaprZ9KOlElAFpOM6V7
M1+0n2h7wZP7W6Y6pBaknMQ0zVx237oMOEGVWOPqv+Qqumg79ZCwL/C/fkG5wOfU
PADzrR18HY1dhla2NkyQJTrjdhMrqn8NSV2wRxwZEAeNtkh1Sk+vCW4nFm0z4GYE
5GGYccIh32hoQbn1bbV81cSsed0c/5DmEZKj+IWMAYHco4E8rk4mH4PbvUeCO0Pf
vObBsZxt0jYk0kY+DRrqHHhsSreQ6TcDqLAxdVQH1WirzaMsKcb26sbhPAYQ2/v+
eZvSQFMD6i4/Sua3uFdHmiegdF1fjE21RQs5NVkjoQKBgQDtCEpS2bxRK9xtUdQV
EGNLBvSDbekmsbclCeOeDIpTKzJWwvNBnMIIbr8glY2mOJS8ci/m5//ja9PEm/n1
09z/TiRjhUZVCZ3xc02vfXbd2blmcfOb/2T4Ru8AmrnRPM0RKS7707/wwROOgQT7
TtS2zFkDR7qyvwWyzEyYNwjp+QKBgQDCmvyLwiw6UlV1FxYmPpXA2WyPkHIpSmGv
8dvoWsMIRW10+cWytmgaug8VNmuIWnNhwxhNee6DGkRbLAnfqakmJYrJwkKwvQUK
BnUmqiHOKwxgSIBFPJY6sMIfWm34NNPivIPw4JzLaDNtBMOB5ie3vlsX5aulniD+
wuHoVOIvCwKBgHF4D4Rb4U+GTK40hu9SegX3nWOxYPR6DK95bXJClJE904o/Jw4Y
ybYZtfsWPMn+ug0AKLhEBXZMLwMmm+E3e8roEHln2oRo5ygnx1+kExwe1ycYk8Lw
3tgPtOg4KY3lqlF1wp0g//hzpTYJw2W+lguwBUiRtOlOkp7GOECB0A2ZAoGAI+BS
wqkvYikfnBB/gtly3jS3MNheU4Jk8/zQn8trvTCfdzs82S+LjbHTagO4W1DhiJpg
9peOAxlqlKxPva4XgvLU3VE03kRak3iRTGl9BfcltvJmw1ziDTvc7c7MC0Ah9+Dt
KhEeltlE8co5CM+XbuZnAMbUmBnI126yGxTbBfUCgYEAoQfz9W03KqpJcNrOYf2I
PA1EHO/kGdcZcE0tgpwJRu3V5pK9XWT6Y2AnbhoH5Pir6QFP4/RCVfrjZmGA+ljn
CQ4c3w0ACIQzo0xssr0OAyb1ZkDKpIJvE5mf6zZn7buw9gUU1lMedSzF6CpD7Ag8
qRbP6veGpJTWDvh9rsc+CqA=
-----END PRIVATE KEY-----
"""


def write_embedded_cert():
    """Materialize the embedded test cert/key to temp files (ssl.load_cert_chain needs paths)."""
    import tempfile

    d = tempfile.mkdtemp(prefix="orc_rest_tls_")
    cert_path = os.path.join(d, "server.crt")
    key_path = os.path.join(d, "server.key")
    with open(cert_path, "w", encoding="ascii") as f:
        f.write(_EMBEDDED_CERT_PEM)
    with open(key_path, "w", encoding="ascii") as f:
        f.write(_EMBEDDED_KEY_PEM)
    return cert_path, key_path


def find_openssl(override=None):
    if override:
        return override if os.path.isfile(override) else None

    found = shutil.which("openssl")
    if found:
        return found

    # openssl ships with Git for Windows but is usually not on PATH.
    candidates = [
        r"C:\Program Files\Git\usr\bin\openssl.exe",
        r"C:\Program Files\Git\mingw64\bin\openssl.exe",
        r"C:\Program Files (x86)\Git\usr\bin\openssl.exe",
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c

    return None


def generate_cert(openssl, cert_path, key_path, cn, days):
    # Self-signed cert with a SAN so WinHTTP name validation passes for localhost/127.0.0.1.
    # -nodes => unencrypted key (no passphrase prompt), matching ssl.load_cert_chain's needs.
    names = [f"DNS:{cn}"]
    if cn != "localhost":
        names.append("DNS:localhost")

    san = "subjectAltName=" + ",".join(names) + ",IP:127.0.0.1"
    cmd = [
        openssl, "req", "-x509", "-newkey", "rsa:2048",
        "-keyout", key_path, "-out", cert_path,
        "-days", str(days), "-nodes",
        "-subj", f"/CN={cn}",
        "-addext", san,
    ]
    print(f"generating self-signed cert: {' '.join(cmd)}", flush=True)
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"openssl failed ({result.returncode}):\n{result.stderr.strip()}")

    print(f"wrote {cert_path} and {key_path}", flush=True)


# Pull a chunk start offset out of whatever the operator's template chose to send it in.
# Order: Content-Range header, then a query param named start/offset/range_start, else 0.
_CONTENT_RANGE = re.compile(r"bytes\s+(\d+)\s*-\s*(\d+)\s*/\s*(\d+|\*)", re.IGNORECASE)


def infer_offset(headers, query):
    cr = headers.get("Content-Range")
    if cr:
        m = _CONTENT_RANGE.search(cr)
        if m:
            return int(m.group(1))

    for key in ("start", "offset", "range_start", "block_start"):
        if key in query:
            try:
                return int(query[key][0])
            except (ValueError, IndexError):
                pass

    return 0


# WebDAV chunked upload: a client MKCOLs an upload directory, PUTs one chunk per request into it, then
# MOVEs "<dir>/.file" onto the destination, and the server concatenates the chunks. ".file" is the
# pseudo-entry Nextcloud uses for this; no real file of that name is ever written.
CHUNK_ASSEMBLY_NAME = ".file"

# Block size for moving a request body between the socket and the disk. The point is that it is a
# constant: an upload of any size costs this much memory, so a multi-GB body does not need a sink host
# with multi-GB of RAM to spare.
BODY_BLOCK = 1024 * 1024


def assemble_chunks(chunk_dir, dst, sort_mode):
    # Concatenate the chunk files of an upload directory into 'dst'. A chunk is a file whose whole name is
    # digits - this engine names each chunk by its byte offset, which is the only per-chunk value a path
    # template can carry. Anything else in the directory is ignored rather than silently concatenated.
    #
    # The ORDER is the point of the test: 'numeric' is what Nextcloud does, and what offset-named chunks
    # depend on ("128" must follow "64"). 'lexicographic' is offered so a scenario can prove that the
    # byte-exact comparison actually discriminates - with it the archive still arrives, but corrupt.
    names = [n for n in os.listdir(chunk_dir) if n.isdigit() and os.path.isfile(os.path.join(chunk_dir, n))]
    names.sort(key=int if sort_mode == "numeric" else str)

    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "wb") as out:
        for name in names:
            with open(os.path.join(chunk_dir, name), "rb") as chunk:
                shutil.copyfileobj(chunk, out)

    return names


# --share-login mock: the cookie name the sink hands out for a share login session, and the HTML page
# served by GET /s/<token>. Real Nextcloud spells its cookie differently and its markup is
# unseen (see the design doc), so this is deliberately a minimal, generic hidden-input form - the point
# is to exercise the dialog's MECHANISM (GET reads three hidden inputs and a cookie, POST validates all
# four posted fields, the PUT is cookie-gated), not to imitate one vendor's page pixel for pixel.
SHARE_SESSION_COOKIE = "orc_share_session"
SHARE_LOGIN_FORM_HTML = """<!DOCTYPE html>
<html><body>
<form method="post" action="/s/{sharing_token}">
<input type="hidden" name="requesttoken" value="{requesttoken}">
<input type="hidden" name="sharingToken" value="{sharing_token}">
<input type="hidden" name="sharingType" value="3">
<input type="password" name="password">
<input type="submit" value="Submit">
</form>
</body></html>
"""


def parse_share_login(spec):
    # 'TOKEN:PASSWORD' -> (token, password), both required and non-empty. Refuses early (before the
    # server starts listening) rather than accepting a half-configured mode that could never authenticate.
    token, sep, password = spec.partition(":")
    if not sep or not token or not password:
        sys.exit("--share-login expects TOKEN:PASSWORD (both non-empty)")

    return token, password


def make_handler(args, expected_auth):
    out_dir = os.path.abspath(args.out)
    os.makedirs(out_dir, exist_ok=True)

    # --challenge-once: the (method, path) pairs already answered with a 401. Shared by every connection.
    challenged = set()
    challenged_lock = threading.Lock()

    # --share-login: the mock server-rendered public-share login. 'None' (the default) means the mode is
    # off and every share-login code path below is skipped, so the sink's behaviour for every other
    # scenario is unchanged.
    share_token, share_password = (None, None)
    share_upload_prefix = None
    if args.share_login:
        share_token, share_password = parse_share_login(args.share_login)
        share_upload_prefix = f"/public.php/dav/files/{share_token}/"

    # session cookie value -> {"requesttoken": str, "authenticated": bool}. Shared by every connection,
    # since the GET, the POST and the PUT of one login dialog can land on different sockets.
    share_sessions = {}
    share_sessions_lock = threading.Lock()

    class Handler(BaseHTTPRequestHandler):
        server_version = "OrcRestTestSink/1.1"
        protocol_version = "HTTP/1.1"  # honor Content-Length per request, keep-alive

        def log_message(self, fmt, *a):
            pass  # we print our own structured lines instead of the default access log

        # --- helpers --------------------------------------------------------

        def _resolve(self, target=None):
            # Map a request path (or a Destination URL) to a local path, sandboxed under out_dir.
            # Returns None on traversal so a hostile path can never escape the output directory.
            raw = urlparse(target if target is not None else self.path).path
            rel = posixpath.normpath(unquote(raw)).lstrip("/")
            if rel in ("", "."):
                return out_dir

            if rel == ".." or rel.startswith("../"):
                return None

            local = os.path.join(out_dir, *rel.split("/"))
            local = os.path.abspath(local)
            if local != out_dir and not local.startswith(out_dir + os.sep):
                return None

            return local

        def _href(self, local):
            # Local path -> URL path the client used, for PROPFIND/<href>.
            rel = os.path.relpath(local, out_dir).replace(os.sep, "/")
            if rel == ".":
                return "/"

            return "/" + quote(rel)

        def _body_length(self):
            return int(self.headers.get("Content-Length", "0") or "0")

        def _read_body(self):
            # For the small bodies of the WebDAV verbs only. The upload path goes through _drain_body:
            # a multi-GB request body must never be held in memory.
            length = self._body_length()
            return self.rfile.read(length) if length else b""

        def _drain_body(self, length, sink=None, offset=0):
            # Move 'length' bytes of request body out of the socket in blocks, writing them at 'offset'
            # in 'sink' - or to nothing when 'sink' is None, which is how a request that will be refused
            # is consumed without being stored. Returns (bytes received, first 64 bytes) so the caller
            # can log the length and honour --dump-body without keeping the body.
            #
            # The destination is opened WITHOUT truncating, so out-of-order and multi-request chunked
            # uploads reassemble by offset. A zero-length body still creates the file.
            preview = b""
            remaining = length
            fd = None
            if sink is not None:
                os.makedirs(os.path.dirname(sink), exist_ok=True)
                flags = os.O_RDWR | os.O_CREAT
                if hasattr(os, "O_BINARY"):
                    flags |= os.O_BINARY

                fd = os.open(sink, flags, 0o644)

            try:
                if fd is not None:
                    os.lseek(fd, offset, os.SEEK_SET)

                while remaining > 0:
                    block = self.rfile.read(min(BODY_BLOCK, remaining))
                    if not block:
                        break  # the client stopped short; the caller reports what did arrive

                    remaining -= len(block)
                    if len(preview) < 64:
                        preview += block[: 64 - len(preview)]

                    if fd is not None:
                        os.write(fd, block)
            finally:
                if fd is not None:
                    os.close(fd)

            return length - remaining, preview

        def _log(self, body_len, note=""):
            parsed = urlparse(self.path)
            print(f"=== {self.command} {self.path}{('  ' + note) if note else ''}", flush=True)
            print(f"  client : {self.client_address[0]}", flush=True)
            print(f"  path   : {parsed.path}", flush=True)
            if parsed.query:
                print(f"  query  : {parsed.query}", flush=True)
            for h, v in self.headers.items():
                print(f"  hdr    : {h}: {v}", flush=True)
            if body_len:
                print(f"  body   : {body_len} byte(s)", flush=True)

        def _reply(self, status, body=b"", content_type=None, extra=None):
            self.send_response(status)
            if content_type:
                self.send_header("Content-Type", content_type)

            self.send_header("Content-Length", str(len(body)))
            for k, v in (extra or {}).items():
                self.send_header(k, v)

            self.end_headers()
            if body and self.command != "HEAD":
                self.wfile.write(body)

            print(f"  -> {status}\n", flush=True)

        def _share_session_id(self):
            # The session cookie value the client sent, or None if it sent none/another cookie.
            for part in self.headers.get("Cookie", "").split(";"):
                name, _, value = part.strip().partition("=")
                if name == SHARE_SESSION_COOKIE:
                    return value

            return None

        def _share_upload_authorized(self, path):
            # True unless --share-login is set and 'path' is the cookie-gated DAV upload path with no
            # authenticated session: every other path, and every sink not started with --share-login, is
            # unaffected. This is the ONLY check the PUT under /public.php/dav/files/<token>/... gets -
            # no HTTP credential of its own, exactly as the real dialog works.
            if share_upload_prefix is None or not path.startswith(share_upload_prefix):
                return True

            with share_sessions_lock:
                session = share_sessions.get(self._share_session_id())

            return bool(session and session["authenticated"])

        def _auth_decision(self):
            # The verdict alone, with no reply: the upload path needs it BEFORE the body is stored (so a
            # refused request writes nothing) but must still answer only once the body is off the socket.
            #
            # With --challenge-once, the first request for each method and path is refused whatever it
            # carries, and the same request sent again is served. A client that resends on a challenge
            # sends every request twice, which is how its per-request checks (certificate pinning) are
            # tested on the resent request.
            if args.challenge_once:
                key = (self.command, self.path)
                with challenged_lock:
                    first = key not in challenged
                    challenged.add(key)

                if first:
                    return False

            if not self._share_upload_authorized(urlparse(self.path).path):
                return False

            if expected_auth is None:
                return True

            return self.headers.get("Authorization", "") == expected_auth

        def _send_unauthorized(self):
            self._reply(401, extra={"WWW-Authenticate": 'Basic realm="orc-test"'})

        def _require_auth(self):
            # Decide and answer in one step, for the verbs whose body is already consumed.
            if self._auth_decision():
                return True

            self._send_unauthorized()
            return False

        def _maybe_redirect(self):
            # With --redirect, bounce any /redirect/<path> to /<path> via a 307 (method preserved), so a client's
            # follow_redirects option can be exercised end to end. The target is same-host (path only) - the safe
            # redirect case that certificate pinning still covers.
            #
            # /redirect-port/<port>/<path> instead sends the client to <path> on another port of this host, as an
            # absolute URL: a new connection to another server, which may present another key. That is how a
            # client's pinning of a redirected request is tested - it must check the new connection too.
            if not args.redirect:
                return False

            parsed = urlparse(self.path)
            if parsed.path.startswith("/redirect-port/"):
                port, _, rest = parsed.path[len("/redirect-port/"):].partition("/")
                if not port.isdigit():
                    return False

                host = (self.headers.get("Host") or "localhost").rsplit(":", 1)[0]
                scheme = "https" if args.tls else "http"
                target = f"{scheme}://{host}:{port}/{rest}"
            elif parsed.path.startswith("/redirect/"):
                target = parsed.path[len("/redirect"):]  # "/redirect/up/x" -> "/up/x"
            else:
                return False

            if parsed.query:
                target += "?" + parsed.query

            self._drain_body(self._body_length())  # drain any request body before replying
            self._log(0, note=f"-> 307 Location: {target}")
            self._reply(307, extra={"Location": target})
            return True

        # --- write verbs (the upload path) ---------------------------------

        def _handle_write(self):
            if self._maybe_redirect():
                return

            parsed = urlparse(self.path)
            query = parse_qs(parsed.query)
            offset = infer_offset(self.headers, query)
            length = self._body_length()

            # Both decisions are taken before a byte is stored, because the body streams straight to its
            # destination: there is no in-memory copy to reconsider afterwards. A request that will be
            # refused is drained to nothing, and the refusal is still sent only once the socket is clear.
            authorized = self._auth_decision()
            local = self._resolve()
            sink = local if (authorized and local is not None) else None

            try:
                received, preview = self._drain_body(length, sink, offset)
            except OSError as e:
                print(f"  write failed: {e}", flush=True)
                self._log(length, note=f"-> offset {offset}")
                self._reply(500)
                return

            self._log(received, note=f"-> offset {offset}")

            if args.dump_body and preview:
                hexs = " ".join(f"{b:02x}" for b in preview)
                ascii_ = "".join(chr(b) if 32 <= b < 127 else "." for b in preview)
                print(f"  data   : {hexs}", flush=True)
                print(f"         : {ascii_}", flush=True)

            if not authorized:
                self._send_unauthorized()
                return

            if local is None:
                self._reply(403)
                return

            self._reply(args.status)

        do_PUT = _handle_write
        do_PATCH = _handle_write

        # --- --share-login mock (server-rendered public-share form login) ---

        def _handle_share_login_get(self):
            # GET /s/<token>: serve the HTML form and start a fresh session - a new requesttoken and a
            # new cookie every time, so a client that never read the page cannot guess the token.
            self._read_body()
            self._log(0)
            requesttoken = f"{secrets.token_hex(8)}:{secrets.token_hex(16)}"
            sid = uuid.uuid4().hex
            with share_sessions_lock:
                share_sessions[sid] = {"requesttoken": requesttoken, "authenticated": False}

            body = SHARE_LOGIN_FORM_HTML.format(
                sharing_token=xml_escape(share_token), requesttoken=xml_escape(requesttoken)
            ).encode("utf-8")
            self._reply(
                200,
                body=body,
                content_type="text/html; charset=utf-8",
                extra={"Set-Cookie": f"{SHARE_SESSION_COOKIE}={sid}; Path=/"},
            )

        def _handle_share_login_post(self):
            # POST /s/<token>: validate all four posted fields against this session's own state. Any
            # mismatch or missing field is a 403 and the session stays unauthenticated - it never falls
            # back to "close enough". A malformed body (not valid urlencoded UTF-8) fails the same way,
            # since it also has no correct 'password' field.
            length = self._body_length()
            body = self.rfile.read(length) if length else b""
            fields = parse_qs(body.decode("utf-8", errors="replace"), keep_blank_values=True)

            def field(name):
                values = fields.get(name)
                return values[0] if values else None

            sid = self._share_session_id()
            with share_sessions_lock:
                session = share_sessions.get(sid)

            ok = (
                session is not None
                and field("password") == share_password
                and field("requestToken") == session["requesttoken"]
                and field("sharingToken") == share_token
                and field("sharingType") == "3"
            )

            self._log(len(body), note=f"-> {'authenticated' if ok else 'refused'}")

            if not ok:
                self._reply(403)
                return

            with share_sessions_lock:
                session["authenticated"] = True

            self._reply(303, extra={"Location": f"/s/{share_token}/download"})

        def do_POST(self):
            if share_token is not None and urlparse(self.path).path == f"/s/{share_token}":
                self._handle_share_login_post()
                return

            self._handle_write()

        # --- WebDAV verbs ---------------------------------------------------

        def do_OPTIONS(self):
            self._read_body()
            self._log(0)
            # OPTIONS is capability discovery; allow it unauthenticated so clients can probe.
            self._reply(200, extra={"DAV": "1, 2", "MS-Author-Via": "DAV", "Allow": DAV_METHODS})

        def do_GET(self):
            if share_token is not None and urlparse(self.path).path == f"/s/{share_token}":
                self._handle_share_login_get()
                return

            if self._maybe_redirect():
                return

            self._read_body()
            self._log(0)
            if not self._require_auth():
                return

            local = self._resolve()
            if local is None or not os.path.exists(local):
                self._reply(404)
                return

            if os.path.isdir(local):
                self._reply(403)  # no directory listing; this is an upload sink
                return

            with open(local, "rb") as f:
                data = f.read()

            self._reply(200, body=data, content_type="application/octet-stream")

        def do_HEAD(self):
            if self._maybe_redirect():
                return

            self._read_body()
            self._log(0)
            if not self._require_auth():
                return

            local = self._resolve()
            if local is None or not os.path.exists(local) or os.path.isdir(local):
                self._reply(404)
                return

            size = os.path.getsize(local)
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(size))
            self.end_headers()
            print("  -> 200\n", flush=True)

        def do_MKCOL(self):
            self._read_body()
            self._log(0)
            if not self._require_auth():
                return

            local = self._resolve()
            if local is None:
                self._reply(403)
                return

            if os.path.exists(local):
                self._reply(405)  # already exists
                return

            if not os.path.isdir(os.path.dirname(local)):
                self._reply(409)  # missing intermediate collection
                return

            os.mkdir(local)
            self._reply(201)

        def do_DELETE(self):
            self._read_body()
            self._log(0)
            if not self._require_auth():
                return

            local = self._resolve()
            if local is None or not os.path.exists(local):
                self._reply(404)
                return

            if os.path.isdir(local):
                shutil.rmtree(local)
            else:
                os.remove(local)

            self._reply(204)

        def _dest_local(self):
            dest = self.headers.get("Destination")
            if not dest:
                return None

            return self._resolve(dest)

        def do_MOVE(self):
            self._read_body()
            self._log(0)
            self._copy_or_move(move=True)

        def do_COPY(self):
            self._read_body()
            self._log(0)
            self._copy_or_move(move=False)

        def _assemble(self, chunk_dir, dst, move):
            existed = os.path.exists(dst)
            try:
                names = assemble_chunks(chunk_dir, dst, args.chunk_sort)
            except OSError as e:
                print(f"  assembly failed: {e}", flush=True)
                self._reply(500)
                return

            print(f"  chunks : {len(names)} assembled in {args.chunk_sort} order -> {self._href(dst)}", flush=True)
            if move:
                shutil.rmtree(chunk_dir, ignore_errors=True)  # a MOVE consumes the upload directory

            self._reply(204 if existed else 201)

        def _copy_or_move(self, move):
            if not self._require_auth():
                return

            src = self._resolve()
            dst = self._dest_local()

            # A MOVE of "<dir>/.file" assembles the chunks of <dir> instead of moving a file: that entry
            # never exists on disk, it names the upload the chunks add up to.
            if src is not None and dst is not None and os.path.basename(src) == CHUNK_ASSEMBLY_NAME:
                chunk_dir = os.path.dirname(src)
                if os.path.isdir(chunk_dir):
                    self._assemble(chunk_dir, dst, move)
                    return

            if src is None or dst is None or not os.path.exists(src):
                self._reply(404 if (src is None or not os.path.exists(src)) else 400)
                return

            existed = os.path.exists(dst)
            if existed and self.headers.get("Overwrite", "T").upper() == "F":
                self._reply(412)
                return

            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if move:
                os.replace(src, dst)
            elif os.path.isdir(src):
                if existed:
                    shutil.rmtree(dst)

                shutil.copytree(src, dst)
            else:
                shutil.copy2(src, dst)

            self._reply(204 if existed else 201)

        def do_PROPFIND(self):
            self._read_body()  # depth/prop request body is ignored; we always report a fixed prop set
            self._log(0)
            if not self._require_auth():
                return

            local = self._resolve()
            if local is None or not os.path.exists(local):
                self._reply(404)
                return

            depth = self.headers.get("Depth", "infinity")
            responses = [self._propfind_one(local)]
            if os.path.isdir(local) and depth in ("1", "infinity"):
                for name in sorted(os.listdir(local)):
                    responses.append(self._propfind_one(os.path.join(local, name)))

            body = (
                '<?xml version="1.0" encoding="utf-8"?>'
                '<D:multistatus xmlns:D="DAV:">' + "".join(responses) + "</D:multistatus>"
            ).encode("utf-8")
            self._reply(207, body=body, content_type='application/xml; charset="utf-8"')

        def _propfind_one(self, local):
            st = os.stat(local)
            is_dir = os.path.isdir(local)
            href = self._href(local)
            if is_dir and not href.endswith("/"):
                href += "/"

            restype = "<D:collection/>" if is_dir else ""
            length = "" if is_dir else f"<D:getcontentlength>{st.st_size}</D:getcontentlength>"
            lastmod = email.utils.formatdate(st.st_mtime, usegmt=True)
            return (
                "<D:response>"
                f"<D:href>{xml_escape(href)}</D:href>"
                "<D:propstat><D:prop>"
                f"<D:resourcetype>{restype}</D:resourcetype>"
                f"{length}"
                f"<D:getlastmodified>{xml_escape(lastmod)}</D:getlastmodified>"
                "</D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat>"
                "</D:response>"
            )

        def do_PROPPATCH(self):
            self._read_body()
            self._log(0)
            if not self._require_auth():
                return

            local = self._resolve()
            if local is None or not os.path.exists(local):
                self._reply(404)
                return

            # Acknowledge every requested property as set (clients often stamp timestamps post-PUT).
            href = self._href(local)
            body = (
                '<?xml version="1.0" encoding="utf-8"?>'
                '<D:multistatus xmlns:D="DAV:"><D:response>'
                f"<D:href>{xml_escape(href)}</D:href>"
                "<D:propstat><D:status>HTTP/1.1 200 OK</D:status></D:propstat>"
                "</D:response></D:multistatus>"
            ).encode("utf-8")
            self._reply(207, body=body, content_type='application/xml; charset="utf-8"')

        def do_LOCK(self):
            self._read_body()
            self._log(0)
            if not self._require_auth():
                return

            token = "opaquelocktoken:" + str(uuid.uuid4())
            href = self._href(self._resolve() or out_dir)
            body = (
                '<?xml version="1.0" encoding="utf-8"?>'
                '<D:prop xmlns:D="DAV:"><D:lockdiscovery><D:activelock>'
                "<D:locktype><D:write/></D:locktype>"
                "<D:lockscope><D:exclusive/></D:lockscope>"
                "<D:depth>infinity</D:depth>"
                "<D:timeout>Second-3600</D:timeout>"
                f"<D:locktoken><D:href>{xml_escape(token)}</D:href></D:locktoken>"
                f"<D:lockroot><D:href>{xml_escape(href)}</D:href></D:lockroot>"
                "</D:activelock></D:lockdiscovery></D:prop>"
            ).encode("utf-8")
            self._reply(200, body=body, content_type='application/xml; charset="utf-8"', extra={"Lock-Token": f"<{token}>"})

        def do_UNLOCK(self):
            self._read_body()
            self._log(0)
            if not self._require_auth():
                return

            self._reply(204)

    return Handler


class _ProxyHandler(BaseHTTPRequestHandler):
    """Minimal forward HTTP proxy supporting CONNECT tunneling and optional Basic auth."""

    server_version = "OrcProxyTestSink/1.1"
    protocol_version = "HTTP/1.1"
    proxy_auth = None  # "user:pass" to require Basic proxy auth; None => open proxy

    def log_message(self, fmt, *a):
        pass  # we print our own structured lines below

    def _require_proxy_auth(self):
        if not self.proxy_auth:
            return True
        expected = "Basic " + base64.b64encode(self.proxy_auth.encode("utf-8")).decode("ascii")
        got = self.headers.get("Proxy-Authorization", "")
        if got == expected:
            return True
        self.send_response(407)
        self.send_header("Proxy-Authenticate", 'Basic realm="orc"')
        self.send_header("Content-Length", "0")
        self.send_header("Proxy-Connection", "close")
        self.end_headers()
        scheme = got.split(" ", 1)[0] if got else "(none)"
        print(f"  proxy auth challenge; client offered scheme={scheme}", flush=True)
        return False

    def do_CONNECT(self):
        print(f"=== CONNECT {self.path}", flush=True)
        print(f"  client : {self.client_address[0]}", flush=True)
        if not self._require_proxy_auth():
            return
        host, _, port = self.path.partition(":")
        port = int(port or 443)
        print(f"  tunnel : {host}:{port}", flush=True)
        try:
            upstream = socket.create_connection((host, port), timeout=10)
        except OSError as exc:
            self.send_error(502, str(exc))
            print(f"  -> 502 ({exc})\n", flush=True)
            return
        self.send_response(200, "Connection Established")
        self.end_headers()
        print("  -> 200 Connection Established\n", flush=True)
        self._tunnel(self.connection, upstream)
        # The CONNECT tunnel consumed the connection; stop BaseHTTPRequestHandler from looping to read
        # another request on the (now-closed) socket, which would raise ConnectionResetError.
        self.close_connection = True

    @staticmethod
    def _tunnel(client, upstream):
        socks = [client, upstream]
        try:
            while True:
                readable, _, errored = select.select(socks, [], socks, 30)
                if errored:
                    break
                for s in readable:
                    try:
                        data = s.recv(65536)
                        if not data:
                            return
                        (upstream if s is client else client).sendall(data)
                    except OSError:
                        # Either side reset/closed the tunnel (e.g. the client aborts after a server
                        # TLS failure) - normal teardown, not a server error. Stop relaying quietly.
                        return
        finally:
            upstream.close()


def run_proxy(port, proxy_auth=None):
    _ProxyHandler.proxy_auth = proxy_auth
    server = ThreadingHTTPServer(("0.0.0.0", port), _ProxyHandler)
    print(f"Forward proxy on :{port} (auth={'on' if proxy_auth else 'off'})", flush=True)
    print("Ctrl-C to stop.\n", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping", flush=True)
        server.shutdown()


def main():
    args = parse_args()

    if args.proxy is not None:
        run_proxy(args.proxy, proxy_auth=args.proxy_auth)
        return

    expected_auth = None
    if args.auth:
        token = base64.b64encode(args.auth.encode("utf-8")).decode("ascii")
        expected_auth = "Basic " + token

    if args.gencert:
        args.tls = True
        args.cert = args.cert or "server.crt"
        args.key = args.key or "server.key"
        if args.force or not (os.path.isfile(args.cert) and os.path.isfile(args.key)):
            openssl = find_openssl(args.openssl)
            if not openssl:
                sys.exit(
                    "--gencert needs openssl; none found on PATH or in Git for Windows. "
                    "Pass --openssl <path>, or generate the cert manually (see this file's header)."
                )

            generate_cert(openssl, args.cert, args.key, args.cn, args.days)
        else:
            print(f"reusing existing {args.cert} / {args.key} (use --force to regenerate)", flush=True)

    handler = make_handler(args, expected_auth)
    httpd = ThreadingHTTPServer((args.host, args.port), handler)

    scheme = "http"
    if args.tls:
        import ssl

        if args.cert or args.key:
            # Never silently fall back to the embedded certificate here. The committed
            # server_cert_pin fixtures pin THAT certificate, so a half-given pair would produce a
            # green pinning run that validated a cert the operator never asked to serve - a
            # fail-open in the one test whose whole job is to prove pinning rejects the wrong key.
            if not (args.cert and args.key):
                sys.exit("--cert and --key go together: pass both, or neither to use the embedded test certificate.")

            certfile, keyfile = args.cert, args.key
        else:
            # No explicit --cert/--key: serve the embedded self-signed cert (no openssl, no external
            # files). It is the cert pinned by server_cert_pin in the rest-proxy*.xml / rest-https-pin.xml
            # fixtures, so pinning works out of the box.
            certfile, keyfile = write_embedded_cert()
            print("serving the embedded test certificate (CN=localhost); pass --cert/--key to override", flush=True)

        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        if args.max_tls:
            # Cap the server's highest offered TLS version so a client that demands a higher floor finds
            # no common protocol: the handshake must fail and the upload must refuse, never downgrade.
            tls_max = {
                "tls1.0": ssl.TLSVersion.TLSv1,
                "tls1.1": ssl.TLSVersion.TLSv1_1,
                "tls1.2": ssl.TLSVersion.TLSv1_2,
                "tls1.3": ssl.TLSVersion.TLSv1_3,
            }
            ctx.maximum_version = tls_max[args.max_tls]
            if args.max_tls in ("tls1.0", "tls1.1"):
                # OpenSSL 3 refuses TLS 1.0/1.1 at its default security level: without this the capped sink
                # could complete no handshake at all, and a client refusing it would prove nothing.
                ctx.minimum_version = ssl.TLSVersion.TLSv1
                ctx.set_ciphers("DEFAULT:@SECLEVEL=0")
            print(f"capping maximum TLS version at {args.max_tls}", flush=True)

        ctx.load_cert_chain(certfile=certfile, keyfile=keyfile)
        httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
        scheme = "https"

    out_dir = os.path.abspath(args.out)
    print(f"REST/WebDAV test sink on {scheme}://{args.host}:{args.port}  ->  {out_dir}", flush=True)
    if expected_auth:
        print("requiring HTTP Basic auth", flush=True)

    print("Ctrl-C to stop.\n", flush=True)

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping", flush=True)
        httpd.shutdown()


if __name__ == "__main__":
    main()
