# Big files

Every other scenario in this bench uploads a few hundred bytes, which says nothing about the sizes a real
collection reaches, nor about the two boundaries the upload path actually has. The `big-*` scenarios
carry a multi-GB payload instead. They are `Heavy`: left out of `-Tag all` and asked for by name,
and the archive and the sink's copy are freed as soon as the verdict is in - the runner otherwise
wipes an output directory only on the NEXT run of the same tag, and two 5.62 GiB archives outlive
the disk.

| tag | payload | what it is about |
|---|---|---|
| `big-http` | 1.87 GiB | big, still below 2 GiB: one plain-HTTP PUT |
| `big-2g` | 3.75 GiB | above 2^31, in one request |
| `big-4g-refused` | 5.62 GiB | above the 4 GB per-request cap: must be refused |
| `big-4g-chunked` | 5.62 GiB | the same payload as 1 GiB chunks: must arrive |
| `big-sigv4` | 1.87 GiB | a big signed body over TLS, to MinIO |

## The payload is concatenated, not stored

A payload that compresses would defeat the test: the archive uploaded, not the file collected, is
what has to reach the size, and `<archive compression="none">` only keeps a size the member already
has. So `BigSeed` (`vmtest.local.psd1`) names one local file whose bytes do not compress - a real
collection archive is the obvious choice - and the runner passes `%ORC_BIG_SOURCES%`, that seed
named once, twice or three times, as the `copy /b` source list in `wolf-main-big.xml`. cmd.exe
concatenates it into ORC's own temp directory, ORC stores it and deletes it, so only the seed sits
on the disk between runs and three sizes cost one file. Unset or missing, the `big-*` scenarios skip
with that reason rather than fail.

`wolf-main-big.xml` is a main configuration, not a fixture: it carries no pin and no credential, so
a scenario's `MainConfig` is anchored on the script directory and needs no re-pinned copy in
`ConfigDir`. It also raises `archive_timeout` from its 5-minute default, which covers writing the
whole archive.

## The 4 GB cap, and the only way past it

A WinHTTP body length is a DWORD, so one request cannot carry more than 4 GB - `SendWholeRemainder`
refuses rather than truncate, and `chunk_size` is the documented way past it, for a server that
accepts a per-chunk offset (against S3 it is not: that needs multipart, which this engine does not
implement). `big-4g-refused` is scored on the refusal, and its fixture uses `operation="move"` on
purpose: a run that believes it delivered deletes its archive, so the archive still being on disk is
what proves the refusal was not taken for a delivery. `big-4g-chunked` is the other half of that
pair - the same payload, 1 GiB at a time, arriving byte-exact.

## What was measured

Against a build of the current tree on this bench, all five pass. `4022765157` and `6034147411` bytes arrive
byte-exact, and the chunked run places chunks at offsets `2147483648` and `4294967296` without a
byte moving: nothing on the path - the archive writer, `Content-Length`, the offsets, the sink's
placement, the hashes - treats a size or an offset as 32-bit signed. AWSv4 signs `UNSIGNED-PAYLOAD`,
so a 1.87 GiB signed PUT does not read the file a second time to sign it.

Two things worth knowing before running these. The sink had to stop reading the body into memory
first (it now streams it in 1 MB blocks: peak `python3` RSS receiving the 3.75 GiB PUT is 20.9 MB),
so an older copy on the host cannot receive these at all - redeploy `rest_test_server.py`. And a
bench binary older than the tree will fail a scenario for the wording of a log line rather than for
its subject; `ExpectLog` here matches only the stable prefix of the refusal, but `Orc` in
`vmtest.local.psd1` pointing at a stale build is worth checking first.

Not covered here: `method="BITS"` and `method="filecopy"` at these sizes, resuming a partial
multi-GB upload, and the WebDAV/Nextcloud chunked dialogs above a few kilobytes.
