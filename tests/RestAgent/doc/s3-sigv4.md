# AWS SigV4 (authenticated S3 uploads)

`authscheme="AWSv4"` signs every request with an AWS Signature Version 4 HMAC, so the dialog no
longer has to be anonymous. `vm-minio-sigv4.xml`, `vm-rustfs-sigv4.xml` and `vm-seaweedfs-sigv4.xml`
are the signed counterparts of `vm-minio.xml`/`vm-rustfs.xml`/`vm-seaweedfs.xml`: byte for byte the
same `<rest>` dialog, uploading to a SEPARATE bucket (`orc-signed`, not `orc`) that grants nothing to
anonymous callers at all. Each server's setup script provisions it beside the existing anonymous one,
which is left completely unchanged - `minio`, `rustfs` and `seaweedfs` keep proving the
unauthenticated dialog on their own bucket, independently of anything below.

The three are not redundant. MinIO and RustFS are close relatives, so a shared misreading of SigV4
would go unnoticed by both; SeaweedFS verifies the signature in an entirely separate implementation
(see **SeaweedFS** in [s3-servers.md](s3-servers.md)). **One signature all three accept is standard SigV4 rather than a dialect.**

| Tag | Fixture | Dialog |
|-----|---------|--------|
| `minio-sigv4` | `vm-minio-sigv4.xml` | signed `PUT /orc-signed/orc-upload/<archive>`, scoped credential |
| `rustfs-sigv4` | `vm-rustfs-sigv4.xml` | same, against RustFS |
| `seaweedfs-sigv4` | `vm-seaweedfs-sigv4.xml` | same, against SeaweedFS |
| `minio-sigv4-badkey` | `vm-minio-sigv4-badkey.xml` | same bucket, wrong `secret_key`, must be refused |

## The six attributes

| Attribute | Required | Meaning |
|-----------|----------|---------|
| `access_key` | yes | the AWS/S3 access key id |
| `secret_key` | yes | the matching secret |
| `session_token` | no | an STS session token, when the credential is temporary (see below) |
| `aws_region` | yes, no default | the region folded into the signing key |
| `aws_service` | no (default `s3`) | the service name folded into the signing key |
| `aws_clock` | no (default `local`) | `local` logs a clock-skew rejection and fails; `server` permits exactly one re-signed retry using the rejecting response's `Date` header |

`aws_region` has no implicit default, not even `us-east-1`: a forgotten attribute would otherwise turn
into a `403` that names nothing useful, which is the hardest SigV4 failure to diagnose blind.
`authscheme="AWSv4"` combined with a literal `Authorization` header, or with any of the headers the
signer itself sets (`Host`, `x-amz-date`, `x-amz-content-sha256`, `x-amz-security-token`), is refused
at embed time rather than producing a request whose meaning depends on header ordering.

## Credentials are not environment-expanded: keep the real key out of the fixture

Like `user`/`password`/`proxy_user`/`proxy_password`, `access_key` and `secret_key` are taken
verbatim (see "Environment variables" in [the README](../README.md)): `%ORC_S3_ACCESS_KEY%` in a fixture arrives as that
literal string, not a resolved value, and a signature computed from it is simply wrong. That is fine
for the throwaway credentials already committed elsewhere in this rig (the squid/proxy accounts,
MinIO's own admin password), but the SigV4 scoped key is provisioned live on whatever bench the
fixture is pointed at, so a literal committed value would be either a real credential in the clear or
a placeholder that quietly fails to authenticate.

`vm-minio-sigv4.xml`, `vm-rustfs-sigv4.xml` and `vm-seaweedfs-sigv4.xml` therefore commit
`access_key`/`secret_key` as obvious placeholders (`REPLACE_WITH_YOUR_ACCESS_KEY` /
`REPLACE_WITH_YOUR_SECRET_KEY`). `make-local-fixtures.ps1` copies them into the git-ignored `local/`
directory unchanged, exactly the "generated copies are yours to edit further" mechanism its own
header already documents for upload/proxy credentials, and the real key goes in there only, taken
from what the matching `setup_*.sh` prints when it creates the `orcupload` account. Never fill it
into the committed fixture.

For a real deployment (not this bench), the equivalent way to keep a long-lived secret out of the
configuration entirely is `session_token` plus a short-lived STS credential: mint one from whatever
issues them (an IAM role, `sts:AssumeRole`, a workload identity federation), pass its access key,
secret and token, and the value in the configuration expires on its own instead of being a standing
credential someone must remember to rotate.

## Path characters are written literally; ORC encodes them once

The signature and the wire request must be computed from the exact same bytes, so `RESTAgent`
percent-encodes the resolved `path` once, signs that, and sends exactly that - `AwsV4Authenticator::PrepareRequest`
marks the request pre-encoded so nothing downstream escapes it again. That means the operator writes
`path=` (and any `{placeholder}` it expands to, such as `{upload.filename}`) with literal
characters, a real space, a real accented letter, never pre-percent-encoded. Writing `%20` for a
space in the fixture would itself get encoded into `%2520` and break both the signature and the
request MinIO/RustFS actually receives.

## A redirect is an error under AWSv4

A signature binds the host, the path and the timestamp it was computed for; WinHTTP has no hook to
re-sign a request in flight, so a followed redirect would arrive at the new host still signed for the
old one. `AwsV4Authenticator::ForbidsRedirects()` returns `true`, which forces
`WINHTTP_OPTION_REDIRECT_POLICY_NEVER` regardless of `follow_redirects` - a `3xx` response is simply an
unacceptable status for the step, ending that `<upload>` the same way any other non-2xx does, rather
than being followed anywhere.

## Unavailable on the XP target

The HMAC-SHA256 primitive (`Crypto/HmacSha256`) is backed by CNG (`bcrypt.dll`), resolved lazily at
run time rather than linked, so a binary built for the `v141_xp` toolset still loads on Windows
XP/2003; it just cannot produce a SigV4 signature there, and `authscheme="AWSv4"` fails closed with a
logged error instead of silently sending an unsigned request.

## `minio-sigv4-badkey`: why this is a wrong `secret_key`, not a wrong `aws_region`

The negative scenario was meant to carry a correct `access_key`/`secret_key` and a deliberately wrong
`aws_region`, which would also prove the region is actually part of what gets signed rather than
accepted and ignored. Measured against this bench and it does not hold: MinIO and RustFS both derive
the signature they expect from whatever region string the request's own `Authorization` header
names, rather than checking it against one the server enforces itself - confirmed on throwaway MinIO
and RustFS instances with `MINIO_REGION`/`RUSTFS_REGION` explicitly pinned to `us-east-1` and a
request signed for `eu-west-9`, which both still answered `200`. A self-consistently wrong region is
therefore never refused by either sink, so it cannot be what this negative scenario tests here. A
wrong `secret_key` exercises the identical `AwsV4Authenticator` refusal path and IS genuinely refused
by both (`SignatureDoesNotMatch`), so that is what `minio-sigv4-badkey.xml` carries instead.
`ExpectLog = SignatureDoesNotMatch` names the cause, so a refusal for some other reason (a bad pin, a
network failure) does not pass.

## Setup

`vm/setup_minio.sh` and `vm/setup_rustfs.sh` create the `orc-signed` bucket (versioned, like `orc`),
an `orcupload` service account, and an IAM policy scoped to `s3:PutObject` on `orc-signed/orc-upload/*`
only - no `s3:GetObject`, no `s3:ListBucket`, and critically no `Principal` at all, so unlike the
anonymous bucket's policy this one grants nothing to anyone who is not that one identity. Both scripts
read the attachment back with `mc admin policy entities` and fail loudly if it did not apply, the same
discipline `setup_rustfs.sh` already used for its anonymous policy against a Public Access Block that
could someday start refusing a wildcard-`Principal` `Allow` (this policy carries no such Principal, so
that specific block should not reach it, but the read-back catches any other silent failure the same
way).

`vm/setup_seaweedfs.sh` reaches the same place by a different route: the scoped identity is three
lines of `/etc/seaweedfs/s3.json` (`"actions": ["Write:orc-signed/orc-upload/*"]`) rather than an IAM
policy attached through an admin API, and there is no wildcard `Principal` anywhere in the model to
guard against. The **trailing `/*` is load-bearing** there - without it every signed PUT is denied.
See **SeaweedFS** in [s3-servers.md](s3-servers.md).

Each server's `fetch_*_sigv4.sh` and `clear_*_sigv4.sh` are the signed bucket's own export/wipe pair,
parallel to the anonymous bucket's, because in none of the three is the stored object the uploaded
bytes: `xl.meta` plus part files on MinIO and RustFS, chunks inside volume files on SeaweedFS.
