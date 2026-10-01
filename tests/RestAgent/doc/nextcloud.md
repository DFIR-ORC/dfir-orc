# Nextcloud

The `nextcloud*` scenarios upload to a real Nextcloud rather than to `rest_test_server.py`. They are
**interop confirmation, not feature coverage**: every mechanism they use is tested by the `webdav*`
scenarios in [the README](../README.md), against the sink. What is left for these is the question no local sink can answer -
does the server operators actually deploy accept this dialog. `vm/setup_nextcloud.sh`
provisions it on the sink host, idempotently:

```
scp tests/RestAgent/vm/setup_nextcloud.sh tests/RestAgent/vm/clear_nextcloud.sh "$SINK":/root/
ssh "$SINK" "bash /root/setup_nextcloud.sh"          # or: bash /root/setup_nextcloud.sh 34.0.4
```

It installs apache2 + PHP + sqlite, unpacks the release, creates an upload-only account, and serves
Nextcloud on port **8448** with the **same `server.crt`/`server.key` the python sinks use** - so the
committed `server_cert_pin` and `make-local-fixtures.ps1` apply to it unchanged, and no fixture needs
`insecure`. It does not generate a certificate: a second one would silently break every other pin.
The credentials it prints (`orc` / `orc-test-not-a-secret`) are the ones the fixtures carry; the
password is longer than the `secret` the other fixtures use only because Nextcloud's password policy
refuses a short one.

| Tag | Fixture | Dialog |
|-----|---------|--------|
| `nextcloud` | `vm-nextcloud.xml` | `MKCOL` the collection (accept 405) + one `PUT` of the whole file |
| `nextcloud-chunked` | `vm-nextcloud-chunked.xml` | chunked upload v2: `MKCOL` collection, `MKCOL` upload dir, chunk `PUT`s, `MOVE` |
| `nextcloud-chunked-many` | `vm-nextcloud-chunked-many.xml` | the same, at `chunk_size="64"` so the rig's archives really split |

Uploads land in the account's own collection, not in `received/`, so those scenarios override where
the runner looks (`/var/nextcloud-data/orc/files/orc-upload`) and what it wipes between runs
(`clear_nextcloud.sh`, which also re-scans so Nextcloud's file cache does not keep naming files that
are gone). The verdict is the same byte-exact comparison as everywhere else.


## The Nextcloud form alternative (`nextcloud-form`)

An **alternative way to upload to Nextcloud**, and the one to reach for when the account WebDAV route
is not available. `setup_nextcloud.sh` provisions a **public link share with a password** on the
`orc-share` folder, and `nextcloud-form` uploads into it through the four-request dialog Nextcloud's
own web client uses: a form login, then a single `PUT` under the public DAV root.

Why it is worth having beside `nextcloud`:

| | `nextcloud` (and `nextcloud-chunked`) | `nextcloud-form` |
|---|---|---|
| endpoint | `/remote.php/dav/files/<user>/` | `/public.php/dav/files/<token>/` |
| credential | an account name and password | no HTTP credential at all - a form login and its session cookie |
| needs | WebDAV enabled, `Authorization` reaching PHP | only what a browser does |
| chunking | yes | no: `/public.php/dav/uploads/` refuses a cookie |

The second column is the one that fails on a hardened deployment. A filtering reverse proxy in front of
Nextcloud may disable WebDAV outright and strip `Authorization`, which kills the account route and
leaves this form dialog as the only one that still works. It is also the only scenario where the
`attribute` capture source meets a real server; [`form-login`](form-login.md) exercises `form_input` against a
mock, and `forminput-vs-nextcloud` below proves the two sources are not interchangeable.

| Tag | Fixture | Dialog |
|-----|---------|--------|
| `nextcloud-form` | `vm-nextcloud-form.xml` | `GET` the authenticate page and capture `data-requesttoken`, `POST` the password with that token, `GET` the share page and capture the token the regenerated session serves, `PUT` under the public DAV root on the cookie. Byte-exact, and `ExpectLog` names `/public.php/dav/files/` so a fixture that degraded to an authenticated `PUT` fails instead of passing on the byte comparison |
| `forminput-vs-nextcloud` | `vm-forminput-vs-nextcloud.xml` | the negative half: the same share and the same login read with `form_input` instead of `attribute`. Nextcloud's dialog is script-rendered, so nothing is found and the capture stops the dialog rather than posting an empty token. `Negative`, with `ExpectLog` on the scan's own refusal so a failure for any other reason does not pass |

Those two are a pair, and the pair is the point: the same share, the same four requests, one capture
source each. `attribute` delivers the file byte-exact; `form_input` finds nothing and refuses, naming
the capture it could not resolve:

```
Form input scan: 'requesttoken' not found in the response body
REST capture 'rt' found no value in the response and has no 'default'; the dialog cannot continue.
```

That is the S5 fail-closed rule, and it is why `form_input` pointed at a Nextcloud fails loudly at the
step that cannot work instead of posting an empty token and collecting an opaque 403 later.

Four requests, not three, and each one earns its place:

- Nextcloud refuses the login `POST` with **412** unless the CSRF token is supplied, as a
  `requesttoken` header or a form field of the same name. Both were tested; the fixture uses the header.
- the login **regenerates the session id**, so the token from the authenticate page is stale afterwards
  and the DAV write needs the one the share page serves. The two differ on the wire.
- the `PUT` itself needs only the session cookie on stock Nextcloud. It carries `requesttoken` and
  `X-Requested-With` because a Nextcloud FORK was observed to refuse the write without them, and
  sending them is harmless here.

The token in the fixture is **forced** by the setup script rather than left as Nextcloud generated it: a
`<rest>` step's `path` is not environment-expanded (only `<upload>`'s own `server`, `uri` and proxy
attributes are), so it has to be written literally in the fixture, and Nextcloud's share API does not
let a caller choose one. The script creates the share through the API and then sets its token in the
database. That is a bench liberty; nothing in a deployment should copy it.

`/index.php` is in the share paths because this bench does not serve pretty URLs. An instance that does
serves the same dialog without the prefix - its own `Location` header says which form it uses. Getting
that wrong is worth recognising: the `POST` is then not processed at all, Nextcloud re-renders the
dialog with **200**, and the failure surfaces one step later as a **401** on the `PUT`.

`Check-NextcloudShare.ps1` drives the same four requests with `curl.exe` against any instance and prints
a verdict. It exists because two different failures both answer 200 with the dialog again - a refused
password and a `POST` that was never routed to the login - and only the page's own
`initial-state-core-publicShareAuth` blob (`invalidPassword: true` versus `null`) tells them apart. Use
it to check a deployment before blaming the engine.

## Chunking into a public share, and why it needs Basic (`nextcloud-share-chunked`)

| Tag | Fixture | Dialog |
|-----|---------|--------|
| `nextcloud-share-chunked` | `vm-nextcloud-share-chunked.xml` | chunked upload v2 into the public share under the share's own Basic credential: `MKCOL` an upload collection, `PUT` each chunk named by its byte offset, `MOVE` the collection's `.file` onto the destination. `ExpectLog` names the SECOND chunk, at `chunk_size="256"`, so a single-chunk upload cannot pass |

**On a public share the share token is the Basic username and the share password is the Basic
password.** Measured: `curl -u '<token>:<password>' -T file .../public.php/dav/files/<token>/name`
answers 201, and so does the same on `/public.php/dav/webdav`.

That is not an alternative to the login dialog, it is a requirement for chunking, because the two
routes reach different collections:

| | session cookie (the login dialog) | Basic (token as username) |
|---|---|---|
| `/public.php/dav/files/<token>/` | works | works |
| `/public.php/dav/uploads/<token>/<id>` | **401**, with any combination of headers | 201 |

So an instance that drops the `Authorization` header before PHP sees it can do the single-PUT login
dialog and cannot chunk at all. The 401 to look for has no `WWW-Authenticate` header and an empty
`<s:message/>`: that is Nextcloud saying no credential arrived, not that the credential was wrong.

Nextcloud's own source explains the other gate: `publicremote.php` refuses **any non-GET** on public
DAV unless `X-Requested-With: XMLHttpRequest` is present or the instance enables outgoing
server-to-server sharing. Stock leaves that on, so this bench accepts the requests without the header;
a hardened instance does not, which is why every step in these fixtures sends it.

## Resuming a chunked upload into a public share

The upload collection exposes a virtual `.file` entry whose length is the **sum of the chunks already
uploaded**, and that number is exactly the offset the next chunk must start at:

```
PROPFIND /public.php/dav/uploads/<token>/<id>/.file   Depth: 0
-> 207  <d:getcontentlength>2000</d:getcontentlength>          (0 for an empty collection)
```

`rest-nextcloud-share-resume.xml` feeds that into `offset=` through a
`<response dav_property="getcontentlength">` capture. `HEAD` is not an alternative - Nextcloud answers
405 on `.file` - so this is the one shape that works, and it needs a build with
`ORC_REST_ENABLE_DAV_PROPERTY=1`; a default build refuses the configuration at embed time and says so.

It carries no rig tag for that reason. It was verified by hand with the flag on, as a real
interrupted-then-resumed transfer rather than a simulated one - two DFIR-ORC runs over the same 3 MiB
payload, against the bench's real share:

| | |
|---|---|
| run 1, partial | a config with **no `MOVE`** uploads four 256 KiB chunks (1 MiB) and stops. The collection's `.file` then reports `getcontentlength 1048576` |
| run 2, resume | `MKCOL` 405 (already there, accepted), `PROPFIND` 207, the capture reads `1048576`, and the **first chunk sent is `/1048576`** - the megabyte already on the server is not sent again. Eight more chunks, then `MOVE` 201 |
| result | the assembled file is byte-identical to the source, by sha256 both through DAV and against the file on the server's own disk |

The demo is also what settles the upload id. It has to be stable across runs **and different per
file**: a literal id puts every file of a collection (archive, log, outcome) into one upload collection,
where their chunks overwrite each other at the same offsets. `orc-resume-{upload.filename}` is stable
for a given file and distinct between files, and `{upload.filename}` is available there because those
are per-file steps, not `session="once"` ones.

A single-PUT resume is not possible against Nextcloud at all, and the failure modes are worth writing
down because the safer-looking configuration is the destructive one. Measured on a 5000-byte file
already on the server:

| what the config sends | status | the remote file afterwards |
|---|---|---|
| `PUT` of the tail alone, **no** `Content-Range` | 204 | **truncated to 4 bytes** |
| `PUT` of the tail **with** `Content-Range` | 400 BadRequest | untouched |
| `PATCH` + `X-Update-Range` (sabre partial update) | 501 on **both** endpoints | untouched |

So `offset=` on a `files/` `PUT` reports success and destroys the remote copy, and adding
`Content-Range` only turns that into a loud refusal - it never produces a resume, because Nextcloud
loads no partial-write plugin anywhere. `HEAD` will happily tell you the remote size with a plain
header capture and no WebDAV parsing, but on that route the number is a verification value with
nothing safe to drive. Resuming means re-entering the chunk protocol.

**And the chunk `PUT`s here deliberately carry no `Content-Range`**, unlike every generic-HTTP chunked
fixture in this repo. There the chunks share one path and the header is the only thing placing them;
here the offset is in the URL, each chunk is its own resource, and Sabre answers **400** to a `PUT` that
arrives with the header. Adding it does not harden the fixture, it breaks every chunk.
## Chunk names are byte offsets, and Nextcloud sorts them numerically

Nextcloud's chunked upload wants one path per chunk, while this engine sends every chunk of a body
step to one path (the offset travels in `Content-Range`). The two meet through `{upload.range_start}`
in the path, which names each chunk by its byte offset - the only per-chunk value a path template can
carry.

That only reassembles correctly if the server orders chunk names numerically: offsets past the tenth
chunk sort differently as text ("10485760" precedes "2097152", "128" precedes "64"). Measured against
**Nextcloud 35**, numeric order is what it uses - 12 x 1 MiB chunks named by offset, and 14 x 64 byte
ones, both reassembled byte-exact, matching a reference upload using sequential `1..N` names. This is
a server behaviour, not a documented contract, which is why `nextcloud-chunked-many` exists: it is the
scenario that would fail if a future version sorted them as strings. A unit test
(`webdav_upload_test.cpp`) keeps its `chunk_size` small enough to stay past that threshold.

`MOVE` names its target in a path-only `Destination` header (also verified accepted). An absolute URL
would have to carry the host, which these fixtures deliberately take from `%ORC_UPLOAD_SERVER%`;
`<header>` values are not environment-expanded, so a host in there would have to be hardcoded.

## Not covered here: resume, and a local-sink Nextcloud fixture

Neither is an oversight:

- **Resume.** The `HEAD` -> `{remote_size}` -> `offset` pattern of `rest-resume.xml` has no meaning
  against Nextcloud. A plain `PUT` *replaces* a file rather than appending, so resuming one would
  upload the tail as the whole new file. The chunked shape does survive an interruption server-side,
  but its upload directory is named `{upload.random:...}`, which is regenerated per file per run, so a
  later run cannot address the directory the earlier one left behind.
- **A Nextcloud-shaped fixture against the local sink.** There is one, and it is not called
  "nextcloud": the dialog is not Nextcloud-specific, so it is tested generically by the `webdav*`
  scenarios in [the README](../README.md). What is left for Nextcloud is interop, which no local sink can answer.

## Unit tests

`tests/OrcLibTest/webdav_upload_test.cpp` covers what needs no server at all, for both families: the
shapes pass `Validate()` (and the negatives - a Basic credential over plain HTTP, `Basic` with no
account - are refused), the DAV templates resolve to the expected paths (notably that the per-file
upload id stays identical across `MKCOL`/`PUT`/`MOVE` while the chunk name advances), and the
committed fixtures still parse into those shapes.

Two of those guard a scenario's *premise* rather than the engine, which the live runs cannot do:
every chunked fixture's `chunk_size` must stay small enough to split the smallest archive this rig can
produce into more than ten chunks - raise it and the scenario silently stops testing the reassembly
order - and `vm-webdav-chunked-lex.xml` must keep describing the same upload as
`vm-webdav-chunked.xml`, since it is a control and only differs in the sink it is pointed at.
