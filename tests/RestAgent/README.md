# REST upload test configs

Ready-made `<upload method="REST">` configurations for exercising `RESTAgent` against the
local WebDAV/REST sink in `./rest_test_server.py` (same directory). A REST upload is an ordered
list of `<rest>` request steps inside `<upload>`: connection/auth/TLS stay on `<upload>`, and each
request (method, path, body, headers) goes in a `<rest>`. Feed them to DFIR-ORC the same way you
drive the repo's `local.xml`.

| Config | Transport | Shape | Sink command |
|--------|-----------|-------|--------------|
| `rest-http.xml`            | HTTP  | whole file, offset in `?start=` query    | `python rest_test_server.py --port 8089 --out .\received --dump-body` |
| `rest-http-chunked.xml`    | HTTP  | 1 MiB chunks, `Content-Range` header     | `python rest_test_server.py --port 8089 --out .\received` |
| `rest-http-basic-auth.xml` | HTTP  | Basic auth via `user`/`password` attrs   | `python rest_test_server.py --port 8089 --out .\received --auth orc:secret` |
| `rest-webdav.xml`          | HTTP  | `MKCOL` collection + `PUT` (multi-step)  | `python rest_test_server.py --port 8089 --out .\received` |
| `rest-resume.xml`          | HTTP  | `HEAD` probe → resume from server offset | `python rest_test_server.py --port 8089 --out .\received` |
| `rest-random.xml`          | HTTP  | two-level sharded `{upload.random}` path | `python rest_test_server.py --port 8089 --out .\received` |
| `rest-fallback.xml`        | HTTP  | fallback list: dead primary → live sink  | `python rest_test_server.py --port 8089 --out .\received` |
| `rest-https-pin.xml`       | HTTPS | self-signed + SPKI pin (secure)          | `python rest_test_server.py --port 8443 --gencert --out .\received` |
| `rest-https-insecure.xml`  | HTTPS | self-signed, validation off (`insecure`) | `python rest_test_server.py --port 8443 --gencert --out .\received` |
| `rest-proxy.xml`           | HTTPS | HTTPS via open forward proxy             | sink: `--port 8443 --tls --out .\received` + proxy: `--proxy 8080` |
| `rest-proxy-auth.xml`      | HTTPS | HTTPS via Basic-auth forward proxy       | sink: `--port 8443 --tls --out .\received` + proxy: `--proxy 8080 --proxy-auth orcproxy:s3cret` |

After a run, the reassembled upload lands under `received\up\` — diff it against the source
archive to confirm a byte-exact transfer.

## Where the rest of it lives

This file holds what every scenario shares: the ports, the step list and placeholders, the
environment variables, the bench, the TLS floor, pinning, authentication and the WebDAV dialog.
Everything tied to one destination sits beside it, one file per target, so reading up on a single
server does not mean reading all of them.

| File | What it covers |
|------|----------------|
| [doc/fallback.md](doc/fallback.md)     | the `<upload>` list, `retry`/`retry_delay`, which files replay, and what the tags prove |
| [doc/destinations.md](doc/destinations.md) | the `<uploads>` block: several destinations, their order, `required`/`artifacts`, filter inheritance, `delete_when_done` |
| [doc/proxy.md](doc/proxy.md)           | the `proxy` attribute, proxy credentials under SYSTEM, starting the proxy, its fixtures |
| [doc/nextcloud.md](doc/nextcloud.md)   | the Nextcloud rig: the form alternative, chunking into a public share, resume, chunk ordering |
| [doc/form-login.md](doc/form-login.md) | login against a server-rendered form, and the mock that serves one on the VM rig |
| [doc/s3-servers.md](doc/s3-servers.md) | MinIO, RustFS and SeaweedFS as anonymous S3 sinks, and what each one gets confusingly wrong |
| [doc/s3-sigv4.md](doc/s3-sigv4.md)     | authenticated S3: the six attributes, credential handling, path encoding, redirects |
| [doc/big-files.md](doc/big-files.md)   | the multi-GB scenarios, the 4 GB per-request cap, and what was measured |

## Environment variables (no hardcoded address)

The `vm-*.xml` fixtures (live tests against a sink host) carry **no hardcoded IP**: their `server=`
and `proxy=` attributes are `%ORC_UPLOAD_SERVER%` / `%ORC_UPLOAD_PROXY%`, expanded at parse time by
the config engine. The **address-bearing** `<upload>` attributes are overridable by the matching
`ORC_UPLOAD_<ATTR>` variable (uppercased attribute name):

| Attribute | Variable | Example value |
|-----------|----------|---------------|
| `server` | `%ORC_UPLOAD_SERVER%` | `https://sink.example:8443` (full URL, scheme + port) |
| `proxy` | `%ORC_UPLOAD_PROXY%` | `sink.example:3128` |
| `proxy_bypass` | `%ORC_UPLOAD_PROXY_BYPASS%` | `*.internal` |

`server`/`uri`/`path` were already env-expanded; `proxy`/`proxy_bypass` were added so the address can
also come from the environment. The names are a convention, not enforced by the engine — any `%VAR%`
in these attributes is expanded, falling back to the literal value when the variable is unset.
**Credentials are intentionally NOT expanded** — `user`/`password`/`proxy_user`/`proxy_password` are
taken verbatim, so a value containing `%` stays literal. `authscheme`/`proxy_authscheme` (enums) and
`server_cert_pin` also stay literal in the fixtures, since the credential scheme and pin are each
test's subject.

### Keeping a credential out of the configuration entirely

Those four attributes being literal does not mean a password has to live in the file. `uri` carries
scheme, credential, host and port in **one** value, and `uri` *is* expanded, so the whole lot can come
from the environment and the configuration itself can name nothing:

```xml
<upload method="REST" uri="%ORC_UPLOAD_URI%" authscheme="Basic" server_cert_pin="...">
```

```
ORC_UPLOAD_URI=https://orc:the-password@sink.example:8448
```

`uri` is exclusive with `server`/`path`/`user`/`password` - set those too and the configuration is
refused - so it is all of the target or none of it. An unset variable is refused rather than dialled,
like `server`.

> Until 2026-09-18 this did not work for any port other than the scheme default: `uri` was parsed for
> a port but the value was dropped when building the server, so `...:8448` silently connected to 443 and
> the upload failed against whatever was, or was not, listening there. Fixed in `OutputSpec.cpp`, with
> the port now going through the same embed-time validation as `server`.

The `vm-*.xml` fixtures deliberately keep using `server=` plus a literal credential: theirs are
throwaway accounts that `vm/setup_squid.sh` and `vm/setup_nextcloud.sh` create on the bench itself, and
spelling them out keeps a scenario readable and `-Tag all` zero-config. The `uri` form is for a real
deployment, where the credential is one the operator must not commit.

`run-vmtest.ps1` sets the `ORC_UPLOAD_*` vars per scenario before launching DFIR-ORC (the child
inherits them). Run `.\run-vmtest.ps1 -Tag all`, or a single/comma-separated `-Tag` (e.g.
`-Tag http,proxy-auth`); `-ListTags` shows every scenario and which ones the current bench can run.
It needs an **elevated** shell — DFIR-ORC links `requireAdministrator`.

## Bench configuration (never committed)

The runner hardcodes nothing about a particular bench. Host address, ssh alias, binary path,
fixture directory and scratch paths all come from a git-ignored settings file:

```powershell
copy vmtest.local.example.psd1 vmtest.local.psd1   # then edit it
```

`vmtest.local.psd1`, `local/`, and any scenario output left inside this directory are ignored by
`tests/RestAgent/.gitignore`. That is deliberate: those files name your test host and hold the
certificate pins and upload credentials your bench uses, none of which belong in the repository.
Every setting can also be passed on the command line for a one-off run (`-VmHost`, `-SshHost`,
`-Orc`, `-ConfigDir`, …), which wins over the file.

**There is no committed working default, by design.** A tracked `vmtest.local.psd1` would be a
trap: `.gitignore` does not apply to an already-tracked path, so the first `git commit -a` after
someone fills in their bench would publish it. The committed file is the *template*, under a
different name, and its values are deliberately unusable — `<name>` for a value to substitute,
`c:\path\to\…` for a path, neither reachable by accident. `run-vmtest.ps1` recognises both forms,
refuses to run while any setting is still one of them, and names the offending keys, so a
half-copied template fails immediately instead of reaching for something real:

```
error : these settings are still template placeholders, fill them in first:
        Orc = c:\path\to\DFIR-ORC.exe
        VmHost = <host>
```

Two binary shapes are supported. A packaged capsule with an embedded `WOLFLAUNCHER_CONFIG` needs
only `Orc`. A raw per-arch build out of the build tree embeds no configuration, so it also needs
`OrcPrefixArgs = @('WolfLauncher')` and a `MainConfig` pointing at a wolf config supplying the
`<archive>`/`<command>` — the `<upload>` still comes from the fixture, via `/local=`, as in a real
deployment. `OutRoot`/`SafeTemp` must be on a **local disk**: the safe-temp check rejects a network
drive, and `run.log` records the host address, so keep them outside the repo.

`SquidMode` (`ntlm` | `basic` | `none`) states which authentication your squid is configured for; a
squid serves one mode at a time, so `-Tag all` runs only the squid scenarios that mode supports.
`vm/setup_squid.sh basic|ntlm` configures it (and switches mode), and the NTLM half needs a domain
controller first, which `vm/setup_addc.sh` provisions with Samba. Note that squid refuses `CONNECT`
to anything outside its `SSL_ports`, and these sinks listen on 8443/8444 rather than 443, which the
script accounts for.

### Fixtures for your own certificate

The committed `vm-https-*.xml` pin the throwaway `localhost` certificate `rest_test_server.py`
embeds (`server.crt`), so they validate as-is against a python sink on the default `VmHost`
(`127.0.0.1`) — a local run needs no extra step. Any other sink serves its own certificate, which
that pin cannot match. Generate your own copies instead:

```powershell
.\make-local-fixtures.ps1          # reads the cert over ssh from the host in vmtest.local.psd1
.\make-local-fixtures.ps1 -CertFile .\my-sink.crt
```

It rewrites `server_cert_pin` with the certificate your sink actually serves and writes the result
to `local\` (or your `ConfigDir`). `vm-https-pin-bad.xml` is left alone — its pin is a *different*
certificate on purpose (`CN=wrong-pin.invalid`, a key nobody holds), since it is the negative test.
Then point `ConfigDir` at that directory. The copies are yours to edit further (credentials, extra
headers); being git-ignored is what makes that safe.

### Not covered here: the zero-byte upload

`RESTAgent::RunBodyStep` has a branch for a file of size 0 (it must still issue one request with an
empty body, rather than report success without sending anything). **No scenario can reach it**, so
do not go looking for one: `UploadMessage::MakeUploadFileRequest` is called only from WolfLauncher,
and every file WolfLauncher queues carries content. Measured on a live bench:

| Candidate | Smallest observed | Why it is never empty |
|-----------|-------------------|-----------------------|
| the archive | 891 B | a 7z always has a header |
| log file, `<file level="critical">` | 295 B | startup records reach the sink before the level applies |
| console file tee | 61 B (7976 B with `/noconsole`) | the journal is written to the tee |
| `<outline>` / `<outcome>` | — | JSON documents, written before upload |

The branch is therefore covered by review only. Reaching it live would need either a genuinely
empty artefact in the upload set or a unit test on the REST body step.

## Ports

`RESTAgent` parses an explicit `:port` out of `server=` (e.g. `https://127.0.0.1:8443`), so these
configs use unprivileged ports (8089/8443) and the sink runs without an elevated shell. Omit the
port to use the scheme default (80/443). A malformed port (a `:` followed by a non-numeric, zero, or
out-of-range value) is refused and the upload fails closed, rather than silently using the default.

## TLS version floor (`https_security`)

`https_security` is a **minimum**: the agent enables that TLS version and every higher one the OS
supports, then refuses to negotiate anything lower. If the server cannot meet the floor, the
handshake fails and the upload fails **closed** - it never downgrades to a weaker protocol.

The VM scenarios prove this against two sinks that top out below what the client requires:
`rest_test_server.py --max-tls tls1.2` on port 8444 and `--max-tls tls1.1` on port 8445, both started by
`start_sinks.sh` (the 1.1 sink lowers OpenSSL's security level, which otherwise refuses TLS 1.1 outright).
Every refusal carries an `ExpectLog` naming the versions the client enabled, so a scenario only passes when
the floor itself held, not on any failure; `tls-ok` and `tls11-ok` are the controls proving each capped sink
completes a handshake at its own ceiling, with the same `server_cert_pin`.

| Tag | Sink | `https_security` | Expected |
|-----|------|------------------|----------|
| `tls-downgrade` | 1.2 | `tls1.3` | refused: "this client requires tls1.3", nothing reaches the sink |
| `tls-ok` | 1.2 | `tls1.3, tls1.2` | byte-exact: negotiates TLS 1.2 and uploads |
| `tls-default-downgrade` | 1.2 | *(absent)* | refused: the default floor is TLS 1.3 |
| `tls-exception-downgrade` | 1.2 | `tls1.3, tls1.2[xp], tls1.2[seven]` | refused: an exception for another OS does not lower the floor here |
| `os-max-downgrade` | 1.2 | `os-max` | refused from build 20348 (locked on TLS 1.3); an older host locks on TLS 1.2 and uploads |
| `os-max-ok` | 8443 | `os-max` | byte-exact, "locked on tls1.3" (tls1.2 before build 20348) |
| `tls11-ok` | 1.1 | `tls1.1`, `insecure` | byte-exact: negotiates TLS 1.1 and uploads |
| `tls12-downgrade` | 1.1 | `tls1.2` | refused: "this client requires tls1.2, tls1.3" |
| `tls12-exception-downgrade` | 1.1 | `tls1.2, tls1.0[xp], tls1.1[seven]`, `insecure` | refused: `insecure` permits the exceptions, which still do not apply here |
| `os-max-insecure-downgrade` | 1.1 | `os-max`, `insecure` | refused: `insecure` adds no fallback below the locked version |
| `os-default-downgrade` | 1.1 | `os-default` | refused before sending: "the connection negotiated TLS 1.1" |

`os-default-downgrade` tests the per-connection proof, which only runs once the system policy has let a
TLS 1.1 handshake complete. Windows 11's WinHTTP default protocols stop at TLS 1.2, so the handshake fails
first there, and the runner reports SKIP unless
`HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Internet Settings\WinHttp\DefaultSecureProtocols` includes
TLS 1.1 (`0x200`). Before Windows 11 an absent value is taken to include it. To run it on a Windows 11 bench,
set that value to `0x2A00` (TLS 1.1 to 1.3) for the run and remove it afterwards: it is machine-wide.

## Certificate pinning across a redirect

With `follow_redirects="true"` WinHTTP may follow a redirect to another server, over a new connection. The
pin must be checked on that connection too: the proof gathered on the first hop is cleared on every redirect
(and on every resend after a 401/407), and a response whose last hop was never checked is refused.

The 8443 sink answers `/redirect-port/<port>/<path>` with a 307 to `<path>` on another port of the same
address. `start_sinks.sh` starts a server on 8446 whose certificate has the same subject and address as
`server.crt` but its own key (`server-other.crt`), so the pin is the only check that can tell it apart:

| Tag | Fixture | Redirected to | Expected |
|-----|---------|---------------|----------|
| `redirect-other-host` | `vm-https-redirect-other-host.xml` | 8444, the pinned key | byte-exact: the new connection matches the pin |
| `redirect-other-key`  | `vm-https-redirect-other-key.xml`  | 8446, another key    | refused: the pin does not match, nothing is sent there |

`redirect-other-host` is the control: the same redirect succeeds with the right key, so the refusal of
`redirect-other-key` is the key, not the redirect.

## Steps & placeholders

Each `<rest>` is one HTTP request. The **body step** (the one with `body="{upload.local_path}"`) streams the
file — exactly one per upload; the others (`MKCOL`, `HEAD`/`PROPFIND`, `MOVE`, …) are control requests
run once. `accept_status="405"` (etc.) accepts known non-2xx codes, e.g. a `MKCOL` on a collection that
already exists. A `<response>` captures one value — a response `header`, or a `dav_property` from a
`PROPFIND` 207 body — into a `{name}` reusable by later steps; used here for resume (`{remote_size}` →
the body step's `offset`, parsed and **clamped to the file size**).

> **Note:** `dav_property` captures are **compiled out by default** (`ORC_REST_ENABLE_DAV_PROPERTY=0`): parsing
> a server-supplied PROPFIND 207 XML body is attack surface, so the XML scanner is not built and a config that
> uses `dav_property` is **refused at embed time**. Use a `header` capture (as `rest-resume.xml` does), or rebuild
> with `-DORC_REST_ENABLE_DAV_PROPERTY=1` to re-enable it.

Placeholders substituted in `path`, `http_query`, `body`, `offset`, and each `<header>`:
`{upload.local_path}` (local file path), `{upload.filename}` (its remote basename), `{upload.filename_base64}`
(base64 of the basename, e.g. for a TUS `Upload-Metadata`), `{upload.total_size}` (its size),
`{upload.range_start}`/`{upload.range_end}`/`{upload.range_length}` (per-chunk on the body step; 0 on control
steps), and any captured `{name}`. The sink reads each chunk's offset from a `Content-Range` header or a
`start`/`offset` query.

`{upload.random:<charset>:<length>[:<id>]}` inserts a random string — `charset` is `hex`/`num`/`alpha`/`alnum`,
`length` is 1–64. It is generated **once per uploaded file** and stays stable across every step and chunk (so all
steps agree on a random path); the next file gets a fresh value. Tokens with the **same** `charset:length` share
one value unless you give them different `:id`s — e.g. a two-level shard `/{upload.random:hex:2:a}/{upload.random:hex:2:b}/…`
yields two independent directories. The value uses a CSPRNG but an unguessable path is not, by itself, an access control.

`rest-random.xml` drives this against the local sink: it `MKCOL`s `/up/` (the local server's `MKCOL` needs the
parent collection to exist), then both shard levels, then `PUT`s the body — so the same random tokens must resolve
identically across all four steps for the body to land where the `MKCOL`s created it.

### What an `ExpectLog` may match in a logged request path

A logged request path has its `{upload.*}` built-ins substituted, but every `<response>` capture value
replaced by a marker naming the capture. A release binary logs

    PUT '/up/uploads/1312742165fe3ae4/64'
    PUT '/files/[redacted:tusloc]/chunk'

So an `ExpectLog` may name a chunk offset, a filename, or a random upload directory - `webdav-chunked`
and `nextcloud-chunked-many` each name a specific offset, precisely so a single-chunk upload cannot
pass for a split one - but it can never name a captured value, because a server chose that and it may
be a session token.

If a scenario needs to assert on a capture value, build a configuration that defines
`ORC_VERBOSE_DIAGNOSTICS` - `Debug` or `RelWithDebInfo` do, and `RelWithDebInfo` is the one that stays
optimized, so it behaves like the shipped binary in every respect except what it writes to its log.
`MinSizeRel` does not, which is the point: it is what ships.

Such a log can carry a captured session token, so keep that binary and its logs to yourself.

## Fallback

`<upload>` is a list: configure several and they are tried **in order** until one succeeds. The file
is uploaded once, by the first entry that works. `rest-fallback.xml` exercises this with a dead
primary (`127.0.0.1:9`, no listener) in front of the live sink; run it against
`python rest_test_server.py --port 8089 --out .\received` and the body lands only via the fallback,
while the run still reports the primary's per-file errors.

Two optional per-entry attributes buy an entry more than its single attempt before the next one is
tried: `retry` (additional attempts, default `0`, at most 10) and `retry_delay` (seconds between two
attempts at the same entry, default `30`, at most 1800), with `retry x retry_delay` capped at 1800
seconds per entry. Retries are exhausted before the next entry starts, never round-robin, and both
attributes apply to every upload method rather than only REST.

The rest is in [doc/fallback.md](doc/fallback.md): why each attempt runs a fresh agent, the
per-entry skip set that keeps a delivered file from being re-sent, why `retry` is warned about on an
async entry instead of refused, which files replay, and what the `retry-exhausted` and
`retry-then-fallback` tags do and do not prove.

## Authentication

REST does only standard, header-based HTTP auth:

- **Basic** — set `authscheme="Basic"` with `user`/`password` (see `rest-http-basic-auth.xml`). The
  agent builds the `Authorization: Basic base64(user:password)` header itself and sends it
  preemptively on every request (no 401 round-trip). `authscheme="Basic"` with no `user` is refused.
- **Bearer / token** — put the credential in a literal `<header>Authorization: Bearer …</header>`.
- **NTLM / Kerberos / Negotiate** — supported with an explicit `user`/`password` (set `authscheme`
  to the scheme). These are challenge-response: the agent pre-sets the credential, then completes the
  SSPI handshake on a single resend after the server's `401` (Basic, by contrast, needs no round-trip).
  The agent forces WinHTTP auto-logon to HIGH, so it never authenticates with the logged-on
  machine/SYSTEM account — only the configured credential — which removes the SSO credential-relay
  exposure those schemes otherwise carry. A credential-less integrated config is refused.

**A refused authentication stops the upload.** When a request gets a `401` or `407` that its step does
not list in `accept_status`, the error names who challenged (server or proxy) and the schemes offered,
for example `HTTP 407 from the proxy, offering Basic, NTLM`. Only the scheme names are logged, never a
header value or a credential. That `<upload>` then sends **nothing else**, for any file or `repeat="Once"`
check left: every further request would present the same credentials again, and each refusal can count
as a failed logon until the directory locks the account out. The files not uploaded are kept for the
next `<upload>`.

## Security notes

- `insecure="true"` authorizes all inherently-insecure transport: plain HTTP, TLS < 1.2, **and**
  disabling TLS certificate validation. Honored in **all builds** (Validate warns when set). Without
  it, REST refuses a weak-transport config.
- `server_cert_pin` (pin the self-signed cert's public key) is the secure path and needs no flag.
- Prefer a scoped, revocable, upload-only credential. A Basic credential (whether via `user`/`password`
  or a hand-built header) is exposed to a MITM over plain HTTP — use HTTPS for anything real.

## The WebDAV dialog (`webdav*`)

`MKCOL`, a control step after the body step, `MOVE`, and one request per chunk each to its own
offset-named path: mechanisms of the upload engine, not of any particular server. They are tested
here against `rest_test_server.py`, so they hold without a Nextcloud anywhere.

| Tag | Fixture | What it covers |
|-----|---------|----------------|
| `webdav` | `vm-webdav.xml` | `MKCOL` + `accept_status="405"`; a `MOVE` **after** the body step, staging the body under a `.part` name so the final name appears only once it is complete |
| `webdav-chunked` | `vm-webdav-chunked.xml` | `{upload.range_start}` in the **path**, so each chunk lands on its own URL; `{upload.random:...}` resolving identically across four steps; server-side assembly on `MOVE` of `<dir>/.file` |
| `webdav-chunked-lex` | `vm-webdav-chunked-lex.xml` | the same upload against a sink that assembles those chunks **lexicographically**. Scored on the sha256 *differing* |

The sink assembles chunks on a `MOVE` of `<dir>/.file` (the pseudo-entry Nextcloud uses): it
concatenates the files in that directory whose names are all digits. `--chunk-sort` chooses the order,
`numeric` by default, and the 8449 sink runs `lexicographic` on purpose.

`webdav-chunked-lex` is the only scenario scored on a **mismatch** (`ExpectMismatch`), and it is
deliberately not a `Negative`: nothing is refused, every request returns 2xx, and the archive does
arrive - at the right size, with the wrong bytes. It earns its place twice over. It proves the
byte-exact comparison every other scenario is graded on actually discriminates rather than passing on
anything that shows up, and it proves the numeric ordering the offset-named chunks depend on is
load-bearing rather than a coincidence they would survive without.
