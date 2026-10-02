# orca

`orca` turns a DFIR-ORC result archive into per-table **parquet** you can query, summarize, and
diff across versions - without expanding the whole result tree. It is a small, no-pip Python tool
(standard library only) that shells out to `duckdb.exe` and `7z.exe`.

---

## Quickstart

**Requirements**

- Python 3.10+ (standard library only - nothing to `pip install`) - https://www.python.org/
- `duckdb.exe` on `PATH` (or in `~/.home/bin`, `C:\Program Files\DuckDB`, `C:\tools\duckdb`) - https://github.com/duckdb/duckdb/releases
- `7z.exe` on `PATH` (or `C:\Program Files\7-Zip\7z.exe`) - https://www.7-zip.org/
- for encrypted `.p7b` results only: the recipient certificate in the Windows store, a loadable
  `libcrypto`, or `openssl` on `PATH` (see **Encrypted results** below)

**Run it** (all three are equivalent):

```powershell
orca <command> ...                 # if tests\orca is on PATH (orca.cmd)
python orca.py <command> ...       # from tests\orca
python -m orca <command> ...
```

**Ingest an archive and look at the results** - the two commands you will use most:

```powershell
# 1. archive -> a run directory of parquet/CSV (does NOT expand the full tree)
orca ingest E:\dumps\DFIR-ORC_HOST.7z --out E:\runs\host

# 2. quick health view of that run
orca report E:\runs\host
```

`ingest` accepts either a single `.7z` archive or a directory containing an ORC Outcome JSON and
its top-level archives.

---

## What a run directory contains

A run is flat: everything it holds sits in one directory, under the name ORC gave it.

```
E:\dumps\ORC_WS_H2ODEV_Offline\
  NTFSInfo_details.parquet    one <Table>.parquet per collected CSV
  GetThis_Default.parquet     (NTFSInfo, USNInfo, GetThis_*, ...)
  ORC_..._Outcome.json        the run's Outcome, under ORC's own name
  ORC_..._Outline.json        every Outline the collection carried (one per run attempt)
  ORC_..._172528.log          the collection-level log, plus each command's own
  Errors\                     the 'Errors' sub-archive, expanded
  getthis.parquet             every collected GetThis sample (written by `orca getthis`)
  dfir-orc.parquet            the wide join (written by `orca merge`)
  csv\                        the same tables as CSV      (`orca ingest`, unless --no-csv)
  csv-trunc\                  CSV with volatile date columns dropped (for readable diffing)
  .orca\                      orca's own bookkeeping, nothing to read by hand
    name-map.csv              table name -> source archive / entry / mtime
    ingest-timing.csv         per-file timing
    manifest.parquet          every leaf file: relpath, size, crc, isCsv (sample blobs included)
    products.txt              what this ingest wrote, so a rebuild removes only that
    cache-source.json         which archive and options produced this run
    skipped.txt               tables --skip-errors could not read (absent when there are none)
```

**`--skip-errors`** (on `ingest` and `diff`). One unreadable collected file otherwise costs
every other table: the first failing job aborts the whole ingest. It happens - on one w11
collection `autoruns.csv` is UTF-16 text of `autorunsc`'s own usage message, saved under a
`.csv` name because the tool failed to run, and duckdb refuses to sniff it. With the flag that
table is skipped and the other 26 are produced.

A run that skipped a table is *incomplete*, and nothing else on disk says so, which is why the
tables and their reasons are recorded in `.orca\skipped.txt`. `diff` reads that file and calls
such a table **unread** rather than added or removed - "the collection stopped producing it"
and "orca could not read it" are not the same finding - and never compares it:

```
  unread tables (skipped at ingest, not compared): autoruns
```

`--one-line` carries `unread_tables` as a count, and only when there is one, so a summary that
mentions it is a summary you should not take at face value. A run ingested with the flag also
gets a cache key of its own: it is not the run a strict ingest would have produced, since that
one would have failed.

The CSV renderings keep a directory of their own: a top-level CSV of the collection
(`VSS_list.csv`) carries the same name as the table ingested from it, and flat they would
overwrite the collected file.

Every parquet column is text (`VARCHAR`): ORC's forensic CSVs carry malformed cells that would
otherwise abort type detection, and text keeps schemas stable across versions for diffing.

---

## Typical command lines

### Collect

Run ORC and leave a result directory the other commands read. `collect` produces archives and
nothing else: ingesting them is `orca ingest`, comparing them is `orca diff`.

```powershell
# a disk image: handed to ORC as is, nothing is mounted
orca collect --offline E:\dumps\w10.dd --orc build\orc.exe --out E:\runs\w10

# a VHDX: attached read-only for the duration of the run, then detached
orca collect --offline E:\vm\w10.vhdx --orc build\orc.exe --out E:\runs\w10 --force

# encrypted output, and arguments passed to ORC verbatim after '--'
orca collect --offline E:\dumps\w10.dd --orc build\orc.exe --out E:\runs\w10 `
    --public-key tools\ci\contoso.com.pem -- /key=GetEvt

# a Hyper-V VM: restore a checkpoint, run, pull the results back to --out
orca collect --vm w10 --checkpoint clean --orc build\orc.exe --out E:\runs\w10 `
    --credential vm.clixml

# then compare two ORC versions on the same input
orca diff --run E:\runs\w10-new --reference E:\runs\w10-old `
    --exclude-files "*.log" "*/Statistics.json" "Capsule#*"
```

`--out` holds the archives, the `local.xml` ORC ran with, `collect.json` (the target, the ORC
binary's sha256, the argv, timings and the exit code), and **the ORC executable itself**: it is
copied in and run from there, so the result directory holds the exact binary that produced it
and your build output is free to be rebuilt while a collection runs. `--no-orc-copy` runs it
where it is instead. A VHD(X) attach needs an elevated shell.

**`--upload` keeps the local archives.** orca writes `operation="copy"` on the generated
`<upload>`, ORC's own default: a result set that has been moved away cannot be ingested or
diffed, and on the VM target `move` empties the very directory the results are pulled from, so
a run that uploaded perfectly would end on a pull with nothing to copy. `--upload-operation
move` asks for the other behaviour, when sending the collection away IS the test.

**`--offline` needs a config that defines `ORC_Offline`.** Passing `/Offline=` makes
WolfLauncher explicitly select that keyword, so a configuration without such an `<archive>`
stops with `Key not found: ORC_Offline`. Production configurations have one; a minimal test
config needs it added. The VM target needs PowerShell, Hyper-V and a guest reachable by PowerShell Direct;
without `--credential` it prompts, and `--credential` reads `.clixml`/`.xml`
(`Get-Credential | Export-Clixml`) or `.json` (`{"user": ..., "password": ...}`).

ORC's own non-zero exit is not treated as a failed collect: the archives it did produce are
kept and the code is returned, because a partial collection is still diffable. A VM run that
produced **no** file is reported the same way: the pull warns which guest directory held
nothing instead of failing on it, `collect.json` records an empty `pulled`, and ORC's exit code
is still what `collect` returns. An `operation="move"` upload reaches that state on purpose.

On the VM target, **what ORC printed comes back as `--out\stdout.log` and `--out\stderr.log`**,
beside the archives. ORC runs there under `Start-Process` in a remote session, so nothing it
prints reaches your console: without those files a guest run that collected nothing can only
report that the results are missing, not why. They are brought back before the archives and
whatever `--no-pull` says, since they are the diagnosis for a run that produced nothing.

In the **guest** they are written outside ORC's output directory, which is what keeps them out
of an `operation="move"` upload and out of the pulled result set that `orca diff` compares file
by file; only the two files are copied out of there, into `--out` itself. A capture directory
that stayed empty because ORC never started comes back as a warning rather than a failure -
`Copy-Item -FromSession` cannot resolve a wildcard that matches nothing, and a diagnostic must
never be the step that fails the collection.

Note that `--out` is then a directory holding `stdout.log`, so `orca report --out` counts those
two among the run's logs, and `orca ingest` copies them into the run beside WolfLauncher's own
log. That is usually what you want from a collection directory; it is a change from them being
tucked away in a subdirectory.

### Ingest

```powershell
# basic: the run lands beside the archive, in E:\dumps\DFIR-ORC_HOST\
orca ingest E:\dumps\DFIR-ORC_HOST.7z

# somewhere else
orca ingest E:\dumps\DFIR-ORC_HOST.7z --out E:\runs\host

# encrypted / password-protected sub-archives (otherwise read from the sibling *_cli_config.xml)
orca ingest E:\dumps\DFIR-ORC_HOST.7z --out E:\runs\host --password S3cret

# parquet only (skip CSV / truncated CSV), more workers
orca ingest E:\dumps\DFIR-ORC_HOST.7z --out E:\runs\host --no-csv --jobs 12

# change which columns count as "volatile" (dropped from csv-trunc)
orca ingest E:\dumps\DFIR-ORC_HOST.7z --out E:\runs\host --exclude-cols SampleCollectionDate CreationDate
```

### GetThis summary

```powershell
# how many files each container archive collected, plus the grand total
orca getthis --run E:\runs\host

# machine-readable
orca getthis --run E:\runs\host --json
```

### Report (run health)

```powershell
orca report E:\runs\host                       # all views
orca report E:\runs\host --view errors         # just one view (outcome | empty | stats | errors)
orca report --root E:\runs                      # discover every run dir under a parent
orca report --root E:\runs --one-line           # one summary line per host
orca report E:\runs\host --json
```

### Diff two versions

```powershell
# straight from the collected archives: each side is ingested once and cached
orca diff E:\dumps\10.3.3\DFIR-ORC_HOST.7z E:\dumps\10.3.1\DFIR-ORC_HOST.7z

# encrypted results, and a result directory (Outcome json + top archives) on either side
orca diff --run E:\dumps\10.3.3 --reference E:\dumps\10.3.1 --private-key contoso.com.key

# compare two whole runs: file-set + CRC, then per-table rows (volatile columns auto-dropped)
orca diff --run E:\runs\host-10.3.3 --reference E:\runs\host-10.3.1

# preview up to 20 differing rows per changed table (readable, column-aware)
orca diff --run E:\runs\host-10.3.3 --reference E:\runs\host-10.3.1 --show

# just two files
orca diff E:\runs\host-10.3.3\getthis.parquet E:\runs\host-10.3.1\getthis.parquet

# focus on file identity only (ignore per-run volatile metadata)
orca diff A\getthis.parquet B\getthis.parquet --columns FullName SHA1 SizeInBytes --show

# export the differing rows to CSV instead of (or as well as) previewing
orca diff --run NEW --reference REF --rows --out-dir E:\runs\diff

# ignore run-local files in the file-set layer (logs, per-run markers)
orca diff --run NEW --reference REF --exclude-files "*.log" "*/Statistics.json" "Capsule#*"

# pair rows on your own identity columns, and widen the change-pattern list
orca diff --run NEW --reference REF --show --key FRN AttrName --patterns 30

# summary shapes
orca diff --run NEW --reference REF --one-line
orca diff --run NEW --reference REF --json
```

**Reading `--show`** - a NEW row and the REF row carrying the same identity (FRN, USN, ...) are
reported as one modification, grouped by *which* columns changed, so a `+1 / -1` on a 46-column
NTFSInfo table names the delta instead of hiding it past the right edge:

```
  paired on FRN, FilenameID, DataID, ADS, VolumeID: 1 modified, 0 added-only, 0 removed-only
  changed: SecDescrID, MD5   (1 row)
    File   FullName  FRN     SecDescrID  MD5
  + $Boot  \$Boot    0x0007  264         bb
  - $Boot  \$Boot    0x0007  263         aa
```

Rows with no counterpart (a pure addition such as new USNInfo records), and tables with no known
identity columns, keep the plain `+`/`-` table. The pairing works on the sampled window, so
`--show N` bounds it; `--key` overrides the built-in per-tool identity.

**Comparing two ORC versions** on the same input, every run-local artifact shows up as a
difference: logs, `Statistics.json`, and the `Capsule#...` marker whose name carries a timestamp
and a GUID. Exclude them to leave only what the tools actually collected:

```powershell
orca diff --run <new>\Archive --reference <old>\Archive `
    --exclude-files "*.log" "*/Statistics.json" "Capsule#*"
```

On a real 10.3.4 vs 10.4.0 pair that turns "27 changed binaries, +1/-1 files" into
`files: +0 / -0  changed-binary: 0`, leaving five genuinely changed tables to look at.

**Reading a run diff** - two runs have the same results when:

```
files: +0 / -0  changed-binary: 0    <- same file set, same binary contents
GetThis_...: +0 / -0 rows            <- every table, no row changes
(no "added tables" / "removed tables")
```

### Encrypted results (`.p7b`)

An ORC `.p7b` is CMS enveloped-data wrapping a journaled archive stream. `orca` reads both
layers itself - the journal replay that used to need `unstream.exe` is built in - and decrypts
with the first of three backends that can:

| backend | needs | notes |
|:---|:---|:---|
| `cng` | the recipient certificate in the Windows `MY` store | no key file at all, and no size limit: it streams |
| `libcrypto` | `--private-key` and a loadable OpenSSL library | CPython ships one on Windows; `libcrypto.so.3` elsewhere |
| `openssl` | `--private-key` and `openssl` on `PATH` | last resort |

**Size matters here.** The two key-file backends decode the whole CMS structure in memory and
fail near 2 GiB - measured, a 1794 MB `.p7b` works and a 2013 MB one does not, in libcrypto and
in the `openssl` executable alike (not a new limit: the PowerShell path had it too). `cng`
decodes incrementally instead, and on the same machine decrypted a **2050 MB `.p7b` in 6.3
seconds at 49 MB of RAM**, where libcrypto peaked at 2887 MB before failing.

So for encrypted results above ~1.5 GB, import the recipient certificate into the Windows `MY`
store and run **without** `--private-key`. orca says exactly that in its error rather than
leaving you with a malloc failure to interpret.

```powershell
orca diff NEW.p7b REF.p7b --private-key contoso.com.key
orca ingest E:\dumps\HOST.p7b --out E:\runs\host --private-key contoso.com.key
orca extract E:\dumps\HOST.p7b --out E:\expanded --cms-backend openssl   # pin one backend
```

If every backend refuses, the error names each one and why, so a missing key and a missing
binary do not look alike.

### Extract (a real tree on disk)

```powershell
# decrypt, expand, and unnest every container - the successor to Expand-OrcArchive
orca extract E:\dumps\DFIR-ORC_HOST.7z --out E:\expanded
orca extract E:\dumps --out E:\expanded --private-key contoso.com.key
orca extract E:\dumps\DFIR-ORC_HOST.7z                 # into the current directory
```

Use `ingest` when you want to query the data; `extract` is for when you genuinely need the
collected files on disk.

Without `--out` the tree lands in the current directory, as any other extractor would put it
there. Nothing is sprayed loose: each archive is expanded into a directory named after it, so
the current directory gains one directory per archive plus the collection's own sidecars.

```
S:\work\                                          the current directory
  ORC_WS_H2ODEV_20260921_172528_Outcome.json      sidecar, unsealed here
  ORC_WS_H2ODEV_20260921_172528_Outline.json
  ORC_WS_H2ODEV_20260921_172528.log
  ORC_WS_H2ODEV_Offline\                          the expanded tree
    NTFSInfo.csv  GetThis\  ...
```

Everything lands under `--out` and **nothing is written back to the collection**: it is
evidence, and reading it must not change it. The Outcome, the Outlines and WolfLauncher's log
are in no archive, so they are put into the tree explicitly - copied when ORC left them in the
clear, decrypted into it when ORC sealed them.

#### `--depth`: how far to unwrap

A layer is any container, and the `.p7b` envelope counts as one, so "only decrypt it" needs no
flag of its own:

| | `--out` holds | on the real collection |
|---|---|---|
| `--depth 1` | the decrypted `ORC_..._General.7z`, nothing expanded | 151 MB |
| `--depth 2` | `ORC_..._General\` with `GetSamples.7z`, `BootCode.7z` ... still packed | 169 MB |
| `--depth 3` | `ORC_..._General\GetSamples\...`, no `.7z` anywhere | 1.6 GB |
| *(default)* | every layer there is | 1.6 GB |

`--depth 2` is the tree earlier versions and `Expand-OrcArchive` produced; from `--depth 3` on,
a container that expanded cleanly leaves the tree, because everything it held is in the tree
now and the archive is an intermediate. One 7-Zip warned about stays, being the only copy of
whatever did not come out.

The count is of layers actually present, so **the same number means different things for an
encrypted and a plain collection**: `--depth 1` on a plain `.7z` expands the archive, there
being no envelope to open first. Sidecars are exempt at every depth - a sealed Outcome is
itself two layers, and handing back `Outcome.json.7z` rather than the Outcome would be a riddle
for the sake of consistency.

#### `--work-dir`: where the intermediates go

```powershell
orca extract E:\dumps --out E:\expanded --work-dir E:\orca-work --private-key contoso.com.key
```

The plaintext of a `.p7b` is the size of the collection, so a temp volume smaller than that
cannot decrypt it at all - and since OpenSSL writes the plaintext itself, a full volume comes
back as `CMS_decrypt failed`, which reads as a wrong key. orca names the free space in that
error rather than leaving you to guess.

`--work-dir` is where those intermediates go instead, and it is a **keep** dir like ingest's:
the decrypted archive lands there under a stable name, and the next `extract` **or `ingest`**
of the same collection prints `reusing` and skips the decrypt. Without it the archive is
scratch and goes when the process ends.

`--refresh` turns that reuse off, the same way it does for `ingest` and `diff`: the kept
archive is decrypted again and the sidecars unsealed again, whatever is already there. The
reuse check is deliberately cheap - the file exists and looks like a 7z - so a copy left behind
by an interrupted run passes it and then fails as `Unexpected end of archive` on every
subsequent run. `--refresh` is how you get out of that without deleting the file by hand.

### Where orca writes

Everything `ingest` and `diff` derive from an archive lands **beside that archive**, in a
directory named after it, so one place holds everything that came out of one collection.
(`extract` is the exception: it writes only under `--out`, the current directory by default, and
leaves the collection untouched - see above.)

```
E:\dumps\
  ORC_WS_H2ODEV_20260921_172528.log             sidecar log, unsealed in place
  ORC_WS_H2ODEV_20260921_172528_Outcome.json    sidecar, unsealed in place
  ORC_WS_H2ODEV_20260921_172528_Outline.json
  ORC_WS_H2ODEV_Offline.7z.p7b                  the collected archive
  ORC_WS_H2ODEV_Offline.7z                      decrypted, kept and reused
  ORC_WS_H2ODEV_Offline\                        the run (flat; see above)
    NTFSInfo_details.parquet  GetThis_Default.parquet  ...
    ORC_..._Outcome.json  ORC_..._Outline.json  ORC_..._172528.log  .orca\
```

A decrypted archive and an unsealed sidecar keep the encrypted file's own name with only the
extension changed (`X.7z.p7b` -> `X.7z`, `X_Outcome.json.7z.p7b` -> `X_Outcome.json`), and both
are reused on the next run: decrypting and replaying a multi-GB collection is the expensive
part of an ingest.

Point orca at the **directory** instead and you get one run for the whole collection - one ORC
run writes one Outcome and one archive per command set, and they ingest together - in a
directory named as ORC named the run, read from its Outcome (or Outline) sidecar:
`\\h2odev\vss\extract\ORC_WS_H2ODEV_20260921_172528\`. A folder of loose archives with no such
sidecar is named after itself.

Two ORC runs dropped in the same directory are an awkward input: there is one Outcome per run and
only the first by name is read, so the other run's archives are named by nothing orca read. They
are opened anyway, each reported as `is not in the Outcome that was read`, rather than dropped in
silence. For `extract` that is the whole story, since every archive gets a directory of its own.
`ingest` still folds them into a single run named after the Outcome it read, so ingest each run
from its own directory when provenance matters.

```powershell
orca ingest E:\dumps\ORC_WS_H2ODEV_Offline.7z          # run lands beside the archive
orca diff NEW.7z REF.7z --work-dir E:\orca-work        # put both runs somewhere with room
orca diff NEW.7z REF.7z --refresh                      # re-decrypt and re-ingest
```

`--work-dir` moves the runs, the decrypted output **and the scratch** somewhere else, all at
once - a collection larger than the temp volume cannot be decrypted otherwise; there the
run directory is named with the ingest key, because two collections' archives often share a
basename. `--out-dir` is unrelated: it is where `--rows` writes its CSVs.

A run is reused when its marker still matches the key of its sources - each archive's name,
size and mtime plus the ingest options, not a content hash, which would mean reading the whole
archive to decide whether to skip work. `--refresh` is the escape hatch. Rebuilding a run
removes only what the previous ingest recorded in `.orca\products.txt`, never the collection's
own files or an expanded tree sharing the directory.

Everything orca does *not* intend to keep - nested containers, the CSV being converted, the
CMS/journal intermediates - goes to `%TEMP%\orca\<pid>` and is removed when the process ends.
`--work-dir` moves that scratch to a volume with room, on every command that takes it. If the
collection cannot be written to (a read-only share or image), an ingest's products go to
scratch too and are simply not reused.

What may be removed is decided by an **index**, never by what a directory is called: each run
writes one in `%TEMP%\orca\` naming the scratch it created, and a directory no index names is
left alone however it is named. The index stays in `%TEMP%` even when `--work-dir` puts the
scratch elsewhere, so a run killed mid-way is cleaned up by a later run that knows nothing
about that directory.

### Outline (one queryable row per ORC run)

The Outline says what ran where: the exact ORC build, the machine, its OS, CPU and memory, the
command line, the configured archives. `ingest` captures every Outline a collection carries
(sealed ones too), and `orca outline` turns them into a table.

```powershell
orca outline --root E:\runs --out E:\runs\fleet.parquet   # across many collections
orca outline E:\runs\host                                  # one run -> <run>\outline.parquet
orca outline E:\runs\host --json                           # just print the rows
```

```
fleet.parquet: 3 run(s) from 2 collection(s)
  TESTHOST  v10.3.4-23-gf265bf56e-x64-xp   Microsoft Windows 10 Enterprise Edition (build 2
  TESTHOST  v10.3.4-62-g48af8529a-x64-xp   Microsoft Windows 10 Enterprise Edition (build 2
```

The fields worth grouping by are real columns - `computer_name`, `version`, `system_type`,
`architecture`, `os_description`, `os_version`, `os_tags`, `cpu_name`, `memory_physical`,
`user_name`, `elevated`, `capsule_sha1`, counts of volumes/drives/adapters/profiles - and the
whole document stays in a `json` column, so anything not promoted is still one `json_extract`
away and no field has to be guessed in advance:

```sql
SELECT version, count(*) AS runs FROM read_parquet('fleet.parquet') GROUP BY version;

SELECT computer_name,
       json_extract_string(json, '$.archives[0].keyword') AS archive,
       json_array_length(json_extract(json, '$.archives[0].commands')) AS commands
FROM read_parquet('fleet.parquet');
```

Note a collection can hold **several** Outlines - ORC writes one per run attempt, and a
restarted run leaves an Outline with no matching Outcome. Each is its own row, keyed by
`run_id`.

### Outcome (one queryable row per command)

Where `outline` says what ran, `outcome` says how it went - one row per command executed, with
its exit code, timing, IO counters and the archive it wrote into.

```powershell
orca outcome --root E:\runs --out E:\runs\commands.parquet
orca outcome E:\runs\host            # one run -> <run>\outcome.parquet
```

```
commands.parquet: 24 command(s) from 2 run(s), 7 failed
  TESTHOST ORC_Offline/USNInfo_offline (code: 11)
  ...
```

Both tables carry `run_id`, so they join - which is how you ask whether a command fails on a
particular build or a particular OS, instead of reading it off two reports:

```sql
SELECT r.version, c.command, c.exit_code
FROM read_parquet('commands.parquet') c
JOIN read_parquet('runs.parquet') r USING (run_id)
WHERE c.ok = 'false' ORDER BY c.command;
```

**Check this before comparing two collections.** A command that failed may have written a
partial table or none at all, and a row-count difference then says nothing about the tool's
behaviour. On a real pair, a USNInfo table looked like it had gained 2179 records in the newer
build; the older run's `USNInfo_offline` had failed with exit code 11 and produced zero rows.

### Merge (one wide row per file)

```powershell
# scalar: most-recent value per column, fast
orca merge --run E:\runs\host

# lossless: keep every distinct value as a recency-ordered array (slower)
orca merge --run E:\runs\host --lossless
```

### Ad-hoc queries

Any parquet in a run is a normal DuckDB table:

```powershell
duckdb -c "SELECT column_name FROM (DESCRIBE SELECT * FROM 'E:/runs/host/getthis.parquet')"
duckdb -c "SELECT FullName, SizeInBytes FROM 'E:/runs/host/getthis.parquet' ORDER BY FullName LIMIT 50"
```

Use forward slashes in DuckDB paths, and cast for numeric ops (`CAST(SizeInBytes AS BIGINT)`),
since columns are text.

---

## Tests

```powershell
python runtests.py            # from tests\orca (bytecode is relocated to %TEMP%)
```

DuckDB-backed integration tests auto-skip when `duckdb` is not on `PATH`.

`collect`'s integration tests run a real ORC collection, so they skip unless the host names
their inputs in the environment (the VHDX one needs an elevated shell for the attach):

```powershell
$env:ORCA_IT_ORC   = "build\orc.exe"      # a configured ORC executable
$env:ORCA_IT_IMAGE = "E:\dumps\w10.dd"    # raw disk image
$env:ORCA_IT_VHDX  = "E:\vm\w10.vhdx"     # .vhd(x), attached read-only
$env:ORCA_IT_KEYWORD = "SystemInfo"       # keep it small; this is the default
python runtests.py
```

They are the only tests that exercise the ORC command line for real, prove ORC accepts the
UTF-8 `local.xml`, and cover the `ctypes` attach plus the drive-letter matching. The VM target
is verified by hand.

---

## Notes

- **No pip.** Only the standard library plus the `duckdb.exe` / `7z.exe` binaries, and - for
  encrypted results only - a CMS backend (Windows certificate store, `libcrypto`, or `openssl`).
- The launcher relocates generated bytecode to `%TEMP%\orca-pycache` so the working tree stays clean.
- Engine is pluggable behind an `Engine` seam; DuckDB is the only implementation today
  (`--engine duckdb`).
