# Proxy

`rest_test_server.py` can run as a **forward HTTP proxy** (CONNECT-tunneling only) in a separate
terminal alongside the upload sink. TLS stays end-to-end — the proxy opens a TCP tunnel so it never
sees plaintext. The two proxy fixtures pin the sink's **embedded** self-signed cert (served by
`--tls` with no `--cert`/`--key`, no openssl), so they run with no setup. That certificate is also
committed as `server.crt`; its key is not a separate file, only `_EMBEDDED_KEY_PEM` inside
`rest_test_server.py` — a throwaway that is valid for `localhost` alone and must never be reused.

## The `proxy` attribute

A single `proxy` attribute on `<upload>` selects how the proxy is found:

| `proxy=` | Meaning |
|---|---|
| *(attribute absent)* | follow the machine-wide WinHTTP configuration (`netsh winhttp`), which connects DIRECT by itself when the machine has no proxy |
| `""` / `"direct"` / `"none"` | force DIRECT, no proxy and no discovery, whatever the machine is configured with |
| `"system"` | machine-wide static proxy (`netsh winhttp`), named explicitly: the auth-capable form |
| `"wpad"` | WPAD auto-detect (DHCP/DNS); **strict** — if no proxy is discovered the upload is **refused** (not silently DIRECT) |
| `"pac:<url>"` | PAC script at `<url>` (explicit); a fetch failure refuses the upload |
| `"<…>.pac"` | PAC script, auto-detected by the `.pac` URL suffix |
| `"host:port"` | explicit named proxy (anything not matching the above) |
| `"%VAR%"` *(still unexpanded)* | the variable is not set on this host: the upload is **refused**, with a message listing the forms above |

Named-vs-PAC is decided by the `.pac` suffix or the `pac:` prefix only — a bare URL like
`http://proxy:8080` is a **named proxy**, never a PAC. `proxy_bypass` applies to a named proxy.

A `wpad`/PAC that successfully resolves to DIRECT (the network's own config chose no proxy) is allowed and
logged at Warn; only a *failure to discover* refuses.

**Absent is not the same as empty.** A config that never writes `proxy=` takes the machine as it finds it,
which is the zero-config case: on a host whose egress requires the machine-wide proxy the upload works with
nothing to configure, and on a host without one WinHTTP connects direct anyway. Writing `proxy=""` is the
operator stating there is no proxy, so nothing is discovered even if the machine has one configured. An
`%ORC_UPLOAD_PROXY%` that expands to nothing lands on that same explicit DIRECT. An **undefined** variable
is different again: Windows leaves `%ORC_UPLOAD_PROXY%` in place rather than failing, so the value names no
reachable host, and the upload is refused when it runs rather than quietly going direct. That check lives in
`HttpClient::Open`, not in `Validate()`, because an unset variable is a property of the host running the
collection: embedding a configuration that reads it must still work on a build machine that does not define
it. Credentials, however, are
never sent to a proxy the config did not name: `proxy_user`/`proxy_password` with no `proxy=` at all is
refused at embed time, and `proxy="system"` is how an operator opts into sending them to the machine proxy.

## Proxy credentials (and SYSTEM)

Proxy authentication always uses the **explicit** `proxy_user`/`proxy_password`/`proxy_authscheme` from
the config — **never** the logged-on user's or the SYSTEM/machine account. ORC runs as SYSTEM, which has
no usable proxy credential of its own and cannot see the per-user (HKCU) proxy config; this is also why
the machine account is never offered (it would be an NTLM-relay vector). So for an authenticating proxy
you **must** embed a credential — use a **scoped, revocable, proxy-only** account, since it is recoverable
from the binary. With no `proxy_user`, a proxy `407` fails the upload closed.

The proxy auth scheme is always taken from the explicit `proxy_authscheme` (`Basic`, `NTLM`, or
`Negotiate`) — never learned from the `407`. `proxy_user`/`proxy_password` with no `proxy_authscheme` is
**refused** at embed time, as is a missing `proxy_user` for an authenticating proxy. **Basic** is presented
preemptively (on the first CONNECT, no `407` round-trip); **NTLM/Negotiate** are challenge-response and
complete on the resend after the `407`. All three are verified end-to-end against a real **squid** proxy
(Basic via `basic_ncsa_auth`; NTLM via Samba `ntlm_auth` + an AD DC).

> **NTLM/Negotiate require a DOMAIN-QUALIFIED `proxy_user`** — `DOMAIN\user` (e.g. `ORCTEST\orc`) or
> `user@domain.fqdn`. WinHTTP sends an *empty* NTLM domain for a bare username, which a real proxy rejects:
> the symptom is a repeating `407` (the proxy logs no authenticated user) and the upload fails closed. A
> bare username with `proxy_authscheme="NTLM"/"Negotiate"` is flagged with a `[W]` hint at config time.
> (Confirmed on squid + Samba: bare `orc` → repeating 407; `ORCTEST\orc` → `TCP_TUNNEL/200`, byte-exact.)

**Proxy credentials go to a proxy or nowhere.** When `HttpClient` opens, it works out whether a proxy
actually handles the connection to the upload host: a named proxy, a `wpad`/PAC result that is not DIRECT,
or for `system` the machine-wide WinHTTP proxy. If none does, including when the host may match the bypass
list, `proxy_user` is not set on any request and a `407` is never answered. A `407` on a direct connection
did not come from a proxy the configuration named. Bypass matching errs towards "bypassed": `*` wildcards
and `<local>` are honoured, and a scheme or port on an entry is ignored. A real proxy that was taken for
bypassed then fails the upload on its `407`, and a `[W]` line at open says the credential was withheld.

### What NTLM and Negotiate to a proxy do not protect

These schemes never send the password itself, but they are weaker than their reputation on this leg:

- **The proxy connection is plaintext.** A `host:port` proxy is reached in clear text, even for an
  `https://` server: the `CONNECT` and its `Proxy-Authorization` exchange come before the TLS tunnel.
  Anyone on the path to the proxy sees the whole NTLM exchange.
- **The NTLM response can be cracked offline.** A captured NTLMv2 challenge/response is enough to brute-force
  the password offline. A weak `proxy_password` falls quickly.
- **It can be relayed.** Nothing binds the exchange to the proxy connection (no channel binding on a
  plaintext leg), so an attacker in the path, or a fake proxy handed out by WPAD, can relay it to another
  service that accepts NTLM from that account.
- **`Kerberos` means `Negotiate`.** `proxy_authscheme="Kerberos"` is an alias. Negotiate picks Kerberos
  when a ticket can be obtained for the proxy and **falls back to NTLM silently** otherwise; WinHTTP has no
  switch to forbid that fallback, so "Kerberos only" cannot be promised.

So the `proxy_user` account must be **dedicated to the proxy**, with a long random password, no other
rights, and nothing it can reach beyond the proxy. Rotate it when a binary carrying it leaves your control.
Avoid `proxy="wpad"` with proxy credentials on a network you do not trust: a spoofed WPAD answer picks the
proxy that receives the exchange.

## Starting the proxy

Open proxy (no credentials required):

```
python rest_test_server.py --proxy 8080
```

Authenticating proxy (Basic, `orcproxy:s3cret`):

```
python rest_test_server.py --proxy 8080 --proxy-auth orcproxy:s3cret
```

`--proxy` runs the proxy process; the upload sink still needs its own terminal
(`python rest_test_server.py --port 8443 --tls --out .\received`).

## Proxy fixtures

| Config | What it tests |
|--------|---------------|
| `rest-proxy.xml`              | HTTPS upload through an **open** forward proxy (`proxy="127.0.0.1:8080"`) |
| `rest-proxy-auth.xml`         | Authenticating proxy, preemptive Basic (`proxy_authscheme="Basic"` → creds on the first CONNECT, no 407) |
| `rest-proxy-auth-reactive.xml`| Negative fixture: `proxy_user` with **no** `proxy_authscheme` → **refused** at embed (auth requires an explicit scheme). |

## Expected observations

**Open proxy (`rest-proxy.xml` against `--proxy 8080`):**

- The proxy logs `=== CONNECT 127.0.0.1:8443` and `-> 200 Connection Established`.
- The upload sink logs the PUT request(s) and returns 200.
- The reassembled file appears under `received\up\`.

**Authenticating proxy (`rest-proxy-auth.xml` against `--proxy 8080 --proxy-auth orcproxy:s3cret`):**

- On the first CONNECT the proxy logs `proxy auth challenge; client offered scheme=(none)` and
  returns 407.
- The agent retries with `Proxy-Authorization: Basic …`; the proxy logs `client offered scheme=Basic`
  and accepts, then logs `-> 200 Connection Established`.
- The upload proceeds as normal.

**Fail-closed check (run `rest-proxy.xml` against the authenticating proxy):**

- No `proxy_user` is configured, so the agent has no credentials to offer.
- The agent must abort with an error indicating the proxy requested authentication (407) and no
  `proxy_user` is configured; no upload body reaches the sink.
- The proxy logs only the `407` challenge line; no `CONNECT` tunnel is established.
