# Form login against a server-rendered form

`rest-form-login.xml` is not part of the local-sink table in [the README](../README.md) and carries no VM rig tag: it logs
into a password-protected public share served by a Nextcloud FORK (stock Nextcloud does not serve
this dialog - see the fixture's own header comment), using a `<response form_input="...">` capture
and a `<form>` urlencoded POST, then uploads under the resulting session cookie.

| Config | What it covers |
|--------|-----------------|
| `rest-form-login.xml` | a server-rendered login form: GET captures three hidden inputs, POST logs in, PUT uploads under the cookie |
| `rest-nextcloud-form.xml` | **a Nextcloud public share, the one to start from.** The four requests its web client makes: capture `data-requesttoken`, POST the password with it, capture the token the regenerated session serves, PUT under the public DAV root |
| `rest-nextcloud-share-chunked.xml` | the same share, CHUNKED, under the share's own Basic credential (the token as the username). The only way to chunk into a public share: `/public.php/dav/uploads/` answers 401 to a session cookie and 201 to Basic |
| `rest-nextcloud-share-resume.xml` | the chunked dialog, RESUMING from the bytes the server already holds, read off the upload collection's virtual `.file`. Needs a build with `ORC_REST_ENABLE_DAV_PROPERTY=1` |

`rest-form-login.xml` was written from a curl transcript before either server had been looked at, and
**no Nextcloud serves hidden inputs**, stock or filtered: they all script-render the
dialog and carry the token on a `<head data-requesttoken>` attribute. It is kept because
`form_input` is still right for a server that renders a real form, but it is not the Nextcloud one.
Use `rest-nextcloud-form.xml`, whose shape the `nextcloud-form` VM scenario proves against a real
instance.

Both carry the share token as the literal placeholder `SHARETOKEN`, to be replaced by hand: a
`<rest>` step's `path` is **not** environment-expanded (only `<upload>`'s own `server`, `uri`,
`proxy` and `proxy_bypass` are), so a `%VAR%` there would reach the wire unexpanded.

`tests/OrcLibTest/rest_form_login_test.cpp` parses the committed fixture and checks it still
describes that shape (three steps, the first with three captures, the second with four form
fields, the third carrying the body, and `Validate()` returning Ok) - the same guard
`webdav_upload_test.cpp` keeps for the WebDAV fixtures.

## The mock, on the VM rig (`form-login*`)

**Why a mock exists at all:** stock Nextcloud does not serve this dialog (see [nextcloud.md](nextcloud.md)), so
`vm/setup_nextcloud.sh` cannot stand in for it the way it does for the plain WebDAV upload. Without a
mock, the GET-scraped `form_input` capture, the `<form>` urlencoded POST, the session cookie carrying
across GET/POST/PUT, and `session="once"` running the login once per agent would have **no coverage
at all** on the VM rig - only the unit tests (`rest_form_login_test.cpp`, and the hostile-input tests
for the pure helpers in `RESTStep.cpp`) would exercise them, and neither runs against a real server or
a real socket. `rest_test_server.py --share-login TOKEN:PASSWORD` (started by `start_sinks.sh` on port
**8460**, same `server.crt`/`server.key` as the other HTTPS sinks) fills that gap with a small,
generic hidden-input form: `GET /s/<token>` hands out a fresh per-session `requesttoken` and a session
cookie, `POST /s/<token>` validates all four posted fields (`password`, `requestToken`, `sharingToken`,
`sharingType`) against that session and authenticates it on success, and a write under
`/public.php/dav/files/<token>/...` is refused with 401 unless that session is authenticated - no HTTP
credential of its own, same as the real dialog.

**What this does and does not prove.** It proves the engine's mechanism: the bounded HTML scan really
reads three hidden inputs out of a live response body, the urlencoded POST really carries the share
password and the captured tokens, the WinHTTP cookie jar really carries the session from the GET onto
the POST and the PUT, and the PUT is really refused without it. It does **not** prove that any real
page matches this mock's markup or field names - the mock's HTML is deliberately generic (an
unadorned `<input type="hidden">` form) rather than a copy of any vendor's page. Nextcloud's own
page is covered by `nextcloud-form` instead; what remains verified only by hand is any OTHER
server that does render a real form, for which `rest-form-login.xml` is the starting point.

| Tag | Fixture | What it covers |
|-----|---------|-----------------|
| `form-login` | `vm-form-login.xml` | the full dialog: GET captures three hidden inputs and a session cookie, POST logs in, PUT uploads under the cookie; byte-exact, and `ExpectLog` names the POST to `/s/orcvmshare1` so a fixture that degraded to a plain PUT fails instead of passing on the byte comparison alone |
| `form-login-nocookie` | `vm-form-login-nocookie.xml` | negative control: the same sink and token, PUT only, no login steps - proves the upload is genuinely cookie-gated. `Negative`, `ExpectLog = 'failed with status 401'` |

`form-login-nocookie` is what makes `form-login`'s PASS mean something: without it, a mock that
accepted any PUT under that path regardless of the login would also score a byte-exact PASS. Together
they are the positive/negative pair every other gated dialog in this rig has (`https-pin`/
`https-pin-bad`, `redirect-other-host`/`redirect-other-key`, ...).
