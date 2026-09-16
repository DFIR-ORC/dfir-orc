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

**Reading a run diff** - two runs have the same results when:

```
files: +0 / -0  changed-binary: 0    <- same file set, same binary contents
GetThis_...: +0 / -0 rows            <- every table, no row changes
(no "added tables" / "removed tables")
```

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

- **No pip.** Only the standard library plus the `duckdb.exe` / `7z.exe` binaries.
- The launcher relocates generated bytecode to `%TEMP%\orca-pycache` so the working tree stays clean.
- Engine is pluggable behind an `Engine` seam; DuckDB is the only implementation today
  (`--engine duckdb`).
