# The three anonymous S3 sinks

MinIO, RustFS and SeaweedFS, each serving the `orc` bucket anonymously. SigV4-authenticated
uploads against these same servers are in [s3-sigv4.md](s3-sigv4.md).

## MinIO (S3)

The `minio` scenario uploads to a real MinIO rather than to `rest_test_server.py`. Like the
`nextcloud*` ones it is **interop confirmation, not feature coverage** - a single `PUT` is already
tested against the sink - but it answers a question the sink cannot: how far can this engine get with
an S3 server at all. `vm/setup_minio.sh` provisions it on the sink host, idempotently:

```
scp tests/RestAgent/vm/setup_minio.sh tests/RestAgent/vm/clear_minio.sh \
    tests/RestAgent/vm/fetch_minio.sh "$SINK":/root/
ssh "$SINK" "bash /root/setup_minio.sh"          # or: bash /root/setup_minio.sh RELEASE.2025-07-23T15-54-02Z
```

It installs the `minio` and `mc` binaries (see below), serves MinIO on port **8450** with the **same
`server.crt`/`server.key` the python sinks use** - so the committed `server_cert_pin` and
`make-local-fixtures.ps1` apply to it unchanged, and the fixture needs no `insecure` - creates the
`orc` bucket, turns versioning on, and attaches the anonymous write policy described below. Like
`setup_nextcloud.sh` it does not generate a certificate: a second one would silently break every pin.

| Tag | Fixture | Dialog |
|-----|---------|--------|
| `minio` | `vm-minio.xml` | one anonymous path-style `PUT /orc/orc-upload/<archive>` |

### The binaries do not come from min.io any more

MinIO **archived** the open-source server and client. `dl.min.io` answers `410 Gone` for every
community release and hotfix - "no longer maintained ... no security updates, or security advisories"
- and the GitHub releases carry a tag with no downloadable asset. What is still published is the
**container image**, which holds both `/usr/bin/minio` and `/usr/bin/mc`, so `setup_minio.sh` lifts
them out of `quay.io/minio/minio:<tag>` with `skopeo` and installs them as ordinary binaries. No
container runtime is involved: MinIO runs as a plain systemd service reading the sink's certificate
straight off the host, which is why nothing else in the script had to change.

The bench is therefore pinned to an archived release by construction (the script defaults to
`RELEASE.2025-09-07T16-13-09Z`). That is acceptable for a sink whose whole job is to answer `PUT`;
do not read the `minio` scenario as a statement that this build is fit to deploy. The `rustfs` and
`seaweedfs` scenarios below are the maintained S3 targets, and `minio` is kept beside them because
MinIO remains widely deployed in the field - the dialog has to keep working against all of them.

### There is exactly one dialog, and it is anonymous

Authenticated S3 requires an **AWS SigV4** signature: an HMAC-SHA256 over a canonical form of the
request, recomputed for every request, keyed on a secret and a timestamp. REST does header-based HTTP
auth only (`Basic`, `NTLM`, `Kerberos`, `Negotiate`) and its placeholders offer neither a clock nor an
HMAC, so **no configuration can produce that signature**. A `<header>Authorization: AWS4-HMAC-SHA256
…</header>` would be a constant, and a constant signature is valid for one request and no other.

So the bucket grants `s3:PutObject` to anonymous callers, on the `orc-upload/` prefix, and nothing
else. What that policy does and does not buy:

- **No `s3:GetObject`.** An anonymous `GET` of an uploaded object is refused, so someone holding the
  collector's configuration cannot read back what was collected.
- **No `s3:ListBucket`.** They cannot enumerate object names either.
- **They can write.** This is the real exposure, and it is not about confidentiality: object keys are
  guessable (ORC names archives by host and timestamp) and an S3 `PUT` *replaces* an object, so an
  anonymous caller who reaches the endpoint could clobber a collection or use the bucket as a file
  drop. `setup_minio.sh` enables **versioning** for exactly that reason - an overwrite becomes a new
  version and the original is still addressable.

Note that this exposure is a property of the *bucket policy*, not of anonymity: an embedded SigV4
access key would be readable by anyone holding the collector too, and scoped to `PutObject` it would
reach exactly the same capability. What signing would add is a **revocable, attributable** credential
and a writable population of "whoever has the collector" rather than "anyone who can reach the
endpoint". Until then, a deployment of this shape should **seal its output** (ORC's CMS encryption),
so that read access to the bucket by any route yields sizes and timestamps rather than evidence.

### No chunking, and no resume

Neither is an oversight, and neither can be fixed in configuration:

- **Multipart.** S3 splits a large object with multipart upload: `POST ?uploads` returns an
  `UploadId` **in an XML response body** (this engine captures response *headers* only, and the DAV
  body scanner is compiled out on purpose), and the final `POST` carries a body that is XML built from
  every part's `ETag` - which no `body=` template can construct. So there is no `minio-chunked`
  companion to `vm-minio.xml`, and an object is capped at S3's **5 GiB single-`PUT` limit**.
- **Resume.** As with Nextcloud, a `PUT` replaces rather than appends, so the `HEAD` → `{remote_size}`
  → `offset` pattern of `rest-resume.xml` would upload the tail as the whole new object.

### The verdict needs the object read back

Every other sink in this rig keeps the upload as a file, so the runner can `sha256sum` it where it
landed. MinIO does not: an object lives as `<bucket>/<key>/xl.meta` plus part files, with small ones
inlined into the metadata, and none of that is the uploaded byte stream. The scenario therefore names
a `FetchScript` (`vm/fetch_minio.sh`), which the runner invokes over ssh after the run and before the
comparison; it exports the prefix with `mc` into `/root/received-minio`, the directory the byte-exact
check then reads. `mc` reads as the administrator, not anonymously - the policy refuses an anonymous
`GET`, which is the property the scenario relies on rather than an obstacle to route around.

`vm/clear_minio.sh` wipes the prefix between runs with `--versions`: versioning is on, so a plain
remove would leave a delete marker with the previous run's bytes addressable underneath.

## RustFS (the second S3 server)

The `rustfs` scenario uploads the same fixture shape as `minio`, to a different S3 implementation, and
that is its whole reason to exist: **one server accepting the dialog says the dialog suits that
server; two say it is S3.** It is the S3 counterpart of what `webdav*` does for DAV, and it also gives
the rig a *maintained* S3 target now that MinIO has archived its open-source server.

`vm/setup_rustfs.sh` provisions it on the sink host, idempotently:

```
scp tests/RestAgent/vm/setup_rustfs.sh tests/RestAgent/vm/clear_rustfs.sh \
    tests/RestAgent/vm/fetch_rustfs.sh "$SINK":/root/
ssh "$SINK" "CLIENT_ADDR=<this machine's address> bash /root/setup_rustfs.sh"
```

**Pass `CLIENT_ADDR`**, the address this machine reaches the sink host from (`echo $SSH_CLIENT` on the
sink names it). Without it the `rustfs-sigv4-port443` scenario cannot run and fails with `sink received
nothing`, because nothing answers on 443.

That scenario needs an S3 server on port **443**, the one URL shape that proves the SigV4 signer strips
a default port from the host it signs. Apache owns `*:443` on this bench - it also serves Nextcloud on
8448 - so the port is not taken over. `setup_rustfs.sh` adds an iptables `PREROUTING` redirect for
`CLIENT_ADDR` alone, which applies before the socket lookup, so that one client reaches RustFS while
apache keeps serving 443 to everyone else. The script skips the redirect silently when `CLIENT_ADDR` is
unset, and the rule does not survive a reboot of the sink host: re-run the script after one.

RustFS is served on port **8452** with the **same `server.crt`/`server.key`** as every other sink, so
the committed pin and `make-local-fixtures.ps1` apply unchanged.

| Tag | Fixture | Dialog |
|-----|---------|--------|
| `rustfs` | `vm-rustfs.xml` | one anonymous path-style `PUT /orc/orc-upload/<archive>` |

### Why its setup is simpler than MinIO's

Nothing in it depends on MinIO, and it needs no container image. RustFS publishes a **static musl
binary** on its GitHub releases, so the install is a `wget`, and the admin steps use **`awscli`**,
which Debian packages - where `setup_minio.sh` has to pull a container image with `skopeo` and untar
the binaries out of its layers. `rustfs tls inspect --path <dir>` also validates the certificate pair
up front, so a TLS mistake is reported by the setup rather than surfacing later as a scenario that
cannot connect.

The one extra step it does have is a **policy read-back**: RustFS documents a Public Access Block that
*refuses* a wildcard-principal `Allow`. It is off by default today, but if a release ever turned it
on, the anonymous policy would silently fail to apply and the upload would start failing with a 403
that looks like an ORC bug. The setup asserts the policy is really in force instead.

### What was measured against RustFS 1.0.0

The interesting result is how little differs. `vm-minio.xml` uploads to RustFS **unchanged** (same
path, same pin, same policy JSON), and:

- anonymous `PUT` → **200**; anonymous `GET` and `LIST` → **403 AccessDenied**, exactly as on MinIO;
- versioning holds: an anonymous overwrite of an uploaded archive lands as a new version and the
  original is still addressable underneath;
- the on-disk layout is the same `xl.meta` as MinIO, so this scenario needs the same `FetchScript`;
- `mc` also drives it fine (only MinIO's proprietary `mc admin info` fails), though the committed
  scripts use `awscli` so that nothing here depends on the MinIO client.

Every limit the MinIO section lists - no multipart, hence one `PUT` and a 5 GiB ceiling, no resume,
and above all no SigV4, hence an anonymous bucket - applies here identically. They are properties of
this engine's S3 dialog, not of either server.

## SeaweedFS (the third S3 server)

`minio` and `rustfs` are two answers to "does a real S3 server accept this dialog". They are also
**close relatives**: same bucket model, the same `xl.meta`-shaped object layout, the same `mc admin`
API for identities. A misreading of S3 they happened to share would pass both. SeaweedFS is the
control: a distributed object store with an **S3 gateway in front of a filer**, its own identity file
instead of an IAM API, and its own on-disk representation. Three independent servers accepting one
dialog is a much stronger statement than two closely related ones.

`vm/setup_seaweedfs.sh` provisions it on the sink host, idempotently:

```
scp tests/RestAgent/vm/setup_seaweedfs.sh tests/RestAgent/vm/clear_seaweedfs.sh \
    tests/RestAgent/vm/fetch_seaweedfs.sh tests/RestAgent/vm/clear_seaweedfs_sigv4.sh \
    tests/RestAgent/vm/fetch_seaweedfs_sigv4.sh "$SINK":/root/
ssh "$SINK" "bash /root/setup_seaweedfs.sh"       # or: bash /root/setup_seaweedfs.sh 4.47
```

SeaweedFS serves S3 on port **8454** with the **same `server.crt`/`server.key`** as every other sink,
so the committed pin and `make-local-fixtures.ps1` apply unchanged. Add `SeaweedFs = 8454` to `Ports`
in `vmtest.local.psd1` only if you moved it.

| Tag | Fixture | Dialog |
|-----|---------|--------|
| `seaweedfs` | `vm-seaweedfs.xml` | one anonymous path-style `PUT /orc/orc-upload/<archive>` |

Like the other two, the install is a `wget` of a **static Go binary** and the admin steps are
`awscli`; nothing here needs a container image or the MinIO client.

### Access control is one json file, not a bucket policy

This is the part that does not look like MinIO or RustFS at all. There is no bucket policy and no
`mc admin`. SeaweedFS reads **one file of identities**, each with credentials and a list of actions,
and anonymous access is simply the identity literally named `anonymous` - the one with no
credentials. Both grants this rig needs are a few lines of `/etc/seaweedfs/s3.json`:

```json
{ "name": "anonymous",  "actions": ["Write:orc/orc-upload/*"] }
{ "name": "orcupload",  "credentials": [ … ], "actions": ["Write:orc-signed/orc-upload/*"] }
```

There is consequently **no Public Access Block to read back around** (the trap `setup_rustfs.sh`
guards against): a wildcard principal is not how any of this is expressed.

### Three things measured against SeaweedFS 4.47, each of which fails confusingly

- **The trailing `/*` is load-bearing.** `Write:orc-signed/orc-upload` and
  `Write:orc-signed/orc-upload/` both **deny every PUT** under that prefix; only the glob form
  matches, because the object is matched with its leading separator. Without it the scenario fails as
  an `AccessDenied` indistinguishable from a bad credential.
- **The default volume cap is spent by the first bucket.** SeaweedFS allocates volumes per
  *collection* - one collection per bucket - and pre-creates several, so with the default of 8 the
  **second** bucket cannot get one and its PUT fails with a **500 `InternalError`**, "no writable
  volumes": a full quota that reads exactly like a broken upload. The setup passes
  `-volume.max=30 -master.volumeSizeLimitMB=1024`.
- **It elects a raft leader before answering anything.** A cold start takes the better part of a
  minute before the S3 gateway responds, far longer than the other two sinks, so the setup's wait
  loop is correspondingly longer. A restart that does not wait for the old process to exit finds the
  ports still held and leaves the service failed - which then fails every scenario for a reason that
  has nothing to do with the upload.

`weed server` also runs a master, a volume server and a filer alongside the gateway, and two of their
default ports are already taken on this bench (volume `8080` is the python http sink, filer `8888` is
the forward proxy). The setup moves all three; only **8454** is ever spoken to from outside.

### What was measured

The anonymous grant behaves exactly as MinIO's and RustFS's bucket policies do: anonymous `PUT` under
the prefix → **200**; anonymous `GET`, `LIST`, a `PUT` outside the prefix, and a `PUT` into the signed
bucket → **403**. The scoped `orcupload` identity writes `orc-signed/orc-upload/` and nothing else -
its `PUT` into the anonymous bucket and its own `GET` are both `AccessDenied` - and a wrong
`secret_key` is refused as `SignatureDoesNotMatch`, so the signature really is verified rather than
waved through.

Against DFIR-ORC itself, both scenarios upload **byte-exact** on the first attempt: `vm-seaweedfs.xml`
and `vm-minio.xml`/`vm-rustfs.xml` are the same fixture shape and SeaweedFS accepts it unchanged, and
`vm-seaweedfs-sigv4.xml` is accepted with the signature this engine produces - no
`SignatureDoesNotMatch`, no clock retry, no per-server accommodation anywhere in the signer.

Every limit the MinIO section lists - no multipart, hence one `PUT` and a 5 GiB ceiling, and no
resume - applies here identically: they are properties of this engine's S3 dialog, not of any server.
The object is not a file on disk here either (chunks inside volume files, addressed through the
filer), so this scenario needs the same `FetchScript` step before the byte-exact comparison.
