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

```
E:\runs\host\
  parquet\           one <Table>.parquet per collected CSV (NTFSInfo, USNInfo, GetThis_*, ...)
  csv\               the same tables as CSV
  csv-trunc\         CSV with volatile date columns dropped (for readable diffing)
  manifest.parquet   every leaf file: relpath, size, crc, isCsv (incl. GetThis sample blobs)
  getthis.parquet    every collected GetThis sample, one row per file (written by `orca getthis`)
  name-map.csv        table name -> source archive / entry / mtime
  ingest-timing.csv   per-file timing
  outcome.json        the run's Outcome (if present)
  outlines/           every Outline the collection carried (one per ORC run attempt)
  logs\               captured .log files
```

Every parquet column is text (`VARCHAR`): ORC's forensic CSVs carry malformed cells that would
otherwise abort type detection, and text keeps schemas stable across versions for diffing.

---

## Typical command lines

### Ingest

```powershell
# basic
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
```

Use `ingest` when you want to query the data; `extract` is for when you genuinely need the
collected files on disk.

### The run cache

`orca diff` given an archive ingests it into a per-user cache and reuses it next time, so
comparing successive builds against the same reference pays for the reference once.

```powershell
orca diff NEW.7z REF.7z --cache-dir E:\orca-cache   # somewhere with room
orca diff NEW.7z REF.7z --refresh                   # re-ingest even if cached
```

Default location is `%LOCALAPPDATA%\orca\cache` (`$XDG_CACHE_HOME/orca` elsewhere). Entries are
keyed by each source archive's name, size and mtime plus the ingest options - not by content,
which would mean reading the whole archive to decide whether to skip work. Cached runs are
ingested parquet-only and are ordinary run directories: `orca report`, `getthis` and `merge`
all work on them. Nothing prunes the cache; delete the directory when it grows too large.

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

---

## Notes

- **No pip.** Only the standard library plus the `duckdb.exe` / `7z.exe` binaries, and - for
  encrypted results only - a CMS backend (Windows certificate store, `libcrypto`, or `openssl`).
- The launcher relocates generated bytecode to `%TEMP%\orca-pycache` so the working tree stays clean.
- Engine is pluggable behind an `Engine` seam; DuckDB is the only implementation today
  (`--engine duckdb`).
