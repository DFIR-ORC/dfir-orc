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
# 1. archive -> a dataset of parquet/CSV (does NOT expand the full tree)
orca ingest E:\dumps\DFIR-ORC_HOST.7z --out E:\datasets\host

# 2. quick health view of that dataset
orca report E:\datasets\host
```

`ingest` accepts either a single `.7z` archive or a directory containing an ORC Outcome JSON and
its top-level archives.

**Before trusting a collection, verify it** - no expansion, bounded disk, non-zero exit if
anything is wrong:

```powershell
orca check E:\dumps\DFIR-ORC_HOST.7z            # see "Check" below
```

---

## Three kinds of directory

orca's commands hand each other three different things, and it helps to keep them apart:

| | what it holds | who writes it |
|:---|:---|:---|
| a **collection** | the archives ORC wrote, plus its Outcome, Outlines and log | ORC, or `orca collect` |
| a **dataset** | per-table parquet, the CSV renderings, the sidecar copies, `.orca` bookkeeping | `orca ingest` |
| an **expanded tree** | the collected files themselves | `orca extract` |

A **run** is one execution of ORC on one machine: what the Outline describes, what `run_id`
identifies, and what a collection is the product of. `check` and `extract` read a collection;
`merge` and `getthis` read a dataset and take it as their argument (`orca merge
E:\datasets\host`) since they read exactly one - and they **ingest it for you** if you hand
them a collection or an archive instead. `report` and `diff` take either kind; `diff` ingests
on demand as well, `report` never does.

## What a dataset contains

A dataset is flat: everything it holds sits in one directory, under the name ORC gave it.

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
    skipped.txt               tables orca could not read (absent when there are none)
```

**One unreadable collected file costs only itself.** It happens - on one w11 collection
`autoruns.csv` is UTF-16 text of `autorunsc`'s own usage message, saved under a `.csv` name
because the tool failed to run, and duckdb refuses to sniff it. That table is skipped and the
other 26 are produced, which is what you want nearly every time: having to ask for it with a
flag would mean asking every time.

**`--strict` is the other answer**, on `ingest`, `extract`, `diff`, `merge` and `getthis`: the
first thing that cannot be read stops the run. For a collection you are about to rely on, a
dataset quietly missing a table is worse than a command that refused to finish. On `extract` it
covers the same ground - an archive the Outcome names and the directory lacks, an empty one, a
container 7-Zip warned about - each of which is otherwise reported and stepped over.

A dataset that skipped a table is *incomplete*, and nothing else on disk says so - which is
why skipping is never quiet. The reason is printed as it happens, the end of the ingest names
the tables where the eye lands, and both go into `.orca\skipped.txt`:

```
skipped 'autoruns': duckdb failed: Invalid Input Error: Error when sniffing file
"...csv_e02gj6sg/autoruns.csv". It was not possible to automatically detect the CSV
parsing dialect The search space used was
1 table(s) skipped, recorded in .orca\skipped.txt: autoruns
```

The per-table line scrolls away behind the 26 tables that worked, which is why the names come
back at the end. `diff` reads that file and calls
such a table **unread** rather than added or removed - "the collection stopped producing it"
and "orca could not read it" are not the same finding - and never compares it:

```
  unread tables (skipped at ingest, not compared): autoruns
```

`--one-line` carries `unread_tables` as a count, and only when there is one, so a summary that
mentions it is a summary you should not take at face value. A run ingested with the flag also
gets a cache key of its own: it is not the dataset a strict ingest would have produced, since that
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
orca collect --offline E:\dumps\w10.dd --orc build\orc.exe --out E:\datasets\w10

# a VHDX: attached read-only for the duration of the run, then detached
orca collect --offline E:\vm\w10.vhdx --orc build\orc.exe --out E:\datasets\w10 --force

# encrypted output, and arguments passed to ORC verbatim after '--'
orca collect --offline E:\dumps\w10.dd --orc build\orc.exe --out E:\datasets\w10 `
    --public-key tools\ci\contoso.com.pem -- /key=GetEvt

# a Hyper-V VM: restore a checkpoint, run, pull the results back to --out
orca collect --vm w10 --checkpoint clean --orc build\orc.exe --out E:\datasets\w10 `
    --credential vm.clixml

# then compare two ORC versions on the same input
orca diff --run E:\datasets\w10-new --reference E:\datasets\w10-old `
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
# basic: the dataset lands in the current directory, in .\DFIR-ORC_HOST\
orca ingest E:\dumps\DFIR-ORC_HOST.7z

# somewhere else
orca ingest E:\dumps\DFIR-ORC_HOST.7z --out E:\datasets\host

# beside the archive, in E:\dumps\DFIR-ORC_HOST\, where any later command finds it
orca ingest E:\dumps\DFIR-ORC_HOST.7z --out-at-input

# encrypted / password-protected sub-archives (otherwise read from the sibling *_cli_config.xml)
orca ingest E:\dumps\DFIR-ORC_HOST.7z --out E:\datasets\host --password S3cret

# parquet only (skip CSV / truncated CSV), more workers
orca ingest E:\dumps\DFIR-ORC_HOST.7z --out E:\datasets\host --no-csv --jobs 12

# change which columns count as "volatile" (dropped from csv-trunc)
orca ingest E:\dumps\DFIR-ORC_HOST.7z --out E:\datasets\host --exclude-cols SampleCollectionDate CreationDate
```

### Fleet (a directory of hosts, one dataset each)

A directory holding several machines' results is several runs. `orca ingest` reads one and
refuses the rest; `orca fleet` reads all of them, one dataset per host:

```
orca fleet C:\share\collect --private-key contoso.com.key                  # into .\<run>\
orca fleet C:\share\collect --private-key contoso.com.key --out-at-input   # into C:\share\collect\<run>\
```

Each dataset is named after its own run, and every table in it carries `OrcComputerName`,
`OrcRunId` and `OrcTimestamp` - as does every table `orca ingest` writes, so a dataset made by
hand answers the same query.

**The fleet database.** `fleet` also writes `fleet.duckdb` beside the datasets: one view per
table across every host of the fleet, so the fleet is one file to open, from any directory. Two
fleets ingested into one directory overwrite each other's; `--out` keeps them apart. Worked
queries - stacking, timelines, timestomping, hashes, WMI persistence - are in
[fleet-database-examples.md](fleet-database-examples.md).

```
fleet database: .\fleet.duckdb (26 view(s), 22 typed; open with: duckdb ".\fleet.duckdb")
```

```sql
-- duckdb fleet.duckdb
-- stacking: the autorun one host has and ninety-nine do not
SELECT "Image Path", "SHA-256", count(DISTINCT OrcComputerName) AS hosts
FROM autoruns GROUP BY ALL ORDER BY hosts ASC;

-- every host's MFT in one table; OrcParquetFile says which volume a row came from
SELECT OrcComputerName, OrcParquetFile, count(*) FROM NTFSInfo GROUP BY ALL;
```

A view is per table **family**, not per file name. ORC names a per-volume table after its volume
(`NTFSInfo_00000000_DiskInterface_0x4e106ff4106fe187_`), so no two hosts share one; the view
`NTFSInfo` holds all of them, whatever the volume. Views, never copies: a host re-ingested in
place is current at once. Re-run `fleet` after adding or removing a host - it reuses every
current dataset, so that costs the views only. The views list their files by absolute path, so
a moved fleet needs that re-run too; a view whose file is gone fails with DuckDB's own
`No files found`, naming it. A view whose files cannot be read is skipped and named, and the
rest of the database is still written.

**Typed views.** Every column is text, as collected. Each table ORC describes, and each table
whose values say what they are, also gets a typed companion, `<family>_typed`: timestamps as
`TIMESTAMP`, sizes, FRNs and USNs as integers (hex or decimal), `Y`/`N` as `BOOLEAN`, GUIDs as
`UUID`; hashes and names stay text. A cell that does
not parse is NULL there and still exact in the text view - a truncated timestamp included, which
a plain cast would have turned into a time nobody recorded.

```sql
SELECT OrcComputerName, FullName, CreationDate FROM NTFSInfo_typed
WHERE CreationDate BETWEEN '2026-10-01' AND '2026-10-06' ORDER BY CreationDate;
```

`--schema` says where the types come from:

```powershell
orca fleet E:\collect                                          # auto: ORC's schemas, then the values
orca fleet E:\collect --schema bundled                         # ORC's schemas only
orca fleet E:\collect --schema infer                           # the values only, every table
orca fleet E:\collect --schema E:\builds\10.4.0\DFIR-ORC.exe   # that binary's schemas, then the values
```

**`auto`, the default: ORC's schemas first, then the values.** ORC's own table schemas
(`*SqlSchema.xml`, copied into `orca/schemas`) type every table ORC describes - its large ones,
NTFSInfo, USNInfo, I30Info, without reading a row. What no schema types is then read from its
values: the tables of the other tools (autoruns, processes, DNS_records, ...) and any column a
newer ORC added. A column the schema calls text stays text - a hash is never re-read as a number.

Reading from the values means checking every non-empty value of a column, across the whole
fleet, against a strict list - `Y`/`N`/`true`/`false`, unsigned (decimal or `0x` hex) and signed
integers, timestamps in ORC's, ISO and autorunsc's formats, GUIDs - and the first that fits them
all is the column's type. One value that fits none keeps the column text, and so does an empty
column; no date whose day and month order depends on the locale (systeminfo's `02/10/2019`) is
ever guessed.

Between ORC versions columns are added, types do not change, so the bundled schemas type any
older collection. A collection made by a **newer** ORC carries columns they do not know; `fleet`
names them and types them from their values meanwhile:

```
  1 table(s) have columns the schema does not know; typed from their values:
    NTFSInfo: NewColumn
  the collection was likely made by a newer ORC than the bundled schema; for its own types,
  pass --schema with the path of the DFIR-ORC.exe that collected it
```

**The alternatives.**

- **A `DFIR-ORC.exe`**: its own schemas in place of the bundled ones, then the values as `auto`
  does - for that newer collection; `orca collect` leaves the binary that ran beside the results.
  It is never run - it is evidence - only its resources are read; a capsule's runners are
  zstd-compressed, which needs Python 3.14, and with an older Python the per-architecture
  `DFIR-ORC_x64.exe` works.
- **`bundled`**: ORC's schemas only. Fastest, and every typed column is one ORC declared; the
  other tools' tables stay text.
- **`infer`**: the values only, for every table. It reads every table, and its answer depends on
  the data: NTFSInfo's `EASize`, `OwnerId` and `VolumeID`, empty on the sample, stay text where
  the schema knows them as integers - another fleet would type them.

On the sample: `auto` typed 22 tables in 14 s (17 by schema, 5 inferred), `bundled` 17 in 3 s,
`infer` 21 in 30 s.

Timestamps are held to their full width before they are parsed, whatever the source:
`'11:4:22.390'` is NULL in a typed view, never `11:04:22`. `orca_schema` says which source typed
the views, `orca_tables.schema` what each was read as - an ORC table (matched by column names,
since nothing in a schema says which collected file it describes), `inferred`, or both - and
`orca_tables.not_in_schema` the columns a schema did not know.

Read the denominator with it - the database carries it too:

| table | holds |
|:---|:---|
| `orca_hosts` | every host and whether it ingested (`.orca-fleet/hosts.csv`) |
| `orca_coverage` | only the gaps: a table a host skipped, or a family every other host produced and this one did not (`.orca-fleet/coverage.csv`) |
| `orca_tables` | one row per view: how many hosts and files it spans, the ORC table a typed view reads, or why it was skipped |
| `orca_schema` | where the typed views' schemas came from |

A stacking result without them describes the hosts that answered while reading as though it
described all of them. Without the database, the same stacking is a glob from the fleet's
directory - `read_parquet('*/autoruns.parquet', union_by_name = true)`, where `union_by_name`
is required: ORC versions and configurations differ across an estate, and a positional union
would misalign the columns.

### GetThis summary

```powershell
# how many files each container archive collected, plus the grand total
orca getthis E:\datasets\host

# machine-readable
orca getthis E:\datasets\host --json
```

### Report (run health)

```powershell
orca report E:\datasets\host                       # the default views
orca report E:\datasets\host --view log            # one view (outcome | stats | empty | log | warn | error | critical | all)
orca report E:\datasets\host --view critical,error         # several, in one list or by repeating the flag
orca report E:\datasets\host --view error --backtrace      # and 5 log lines either side of each
orca report E:\datasets\host --view error --backtrace 10   # 10 lines either side instead
orca report E:\datasets\host --no-archive-logs      # do not open the archives for their logs
orca report E:\datasets                      # every run or collection under a parent
orca report E:\datasets --one-line           # one summary line per host
orca report E:\datasets\host --json                 # one flat JSON object per host
orca report E:\datasets --list               # one line per command, every host
orca report E:\datasets --list --failed      # only the commands that did not exit 0
orca report \\host\dumps\ORC_HOST --private-key contoso.com.key   # a sealed collection
orca report E:\datasets --ignore "Azure*,PersistenceSniper"   # pass over what you already know about
orca report E:\datasets --ignore-file ignore.json    # the same, from a JSON list
orca report                                  # in a fleet's output directory: the whole fleet
orca report fleet.duckdb --one-line          # the same, named by its database
```

`report` also holds the two row tables: `orca report outcome` and `orca report outline` (below).

#### What a directory means

```powershell
# a merged dump: one directory, one machine's files beside the next
orca report \\host\archive\merged --private-key contoso.com.key --one-line
DESKTOP-U9N26B0: ok=False cmds=35 fails=12 ...
w11test:         ok=False cmds=35 fails=12 ...

# a directory of per-host folders: ask to look inside
orca report E:\datasets --recurse --one-line
```

**The Outcome identifies a run, not the directory.** A fleet arrives either as a directory of
per-host folders or as one directory holding every machine's files side by side, and the second
shape used to report the first Outcome found and silently drop the rest - two machines out of
three, with nothing saying so. Every Outcome in a directory is now its own run, and so is every
child directory that holds one. There was a `--root` for the first shape; it has no job left and
is gone.

**Only the directories you name, unless you ask for more.** Asked about a directory, `report`
answers about that directory - which is all a merged dump needs, its collections being right
there. `--recurse` looks in everything underneath, which is what a directory of per-host
folders or a tree of dumps by date needs:

```powershell
orca report \\host\archive --recurse --private-key contoso.com.key --one-line
DESKTOP-U9N26B0: ok=False cmds=35 fails=12 ... dur=8m2s
DESKTOP-U9N26B0: ok=False cmds=35 fails=12 ... dur=6m47s
H2ODEV:          ok=False cmds=12 fails=7  ... dur=54m35s
w11test:         ok=False cmds=35 fails=12 ... dur=7m37s
```

One machine with several collections is several runs, as above: each Outcome is one. When a
directory holds no collection, the error lists what would have worked - a collection, a dataset,
a fleet's output directory or its `fleet.duckdb`, or `--recurse` to look inside.

Where the same run is seen twice - a collection, the dataset made from it, an expanded tree -
the sighting with an ingest timing beside it wins: that is the one with tables to report; of two
datasets, the more recently ingested. That de-duplication by run id is what makes `--recurse`
safe to point at a tree. A dataset carrying no Outcome at all is still a run, since its tables
are worth reporting.

#### A fleet, and a collection's datasets

`report` meets `fleet` without being told how:

- **A fleet's output directory is reported as a fleet.** It is known by `.orca-fleet/hosts.csv`,
  the list of every host and its dataset - failed ones included. No `--recurse`, and no path at
  all when it is the current directory; `fleet.duckdb` names the same directory.
- **A collection finds its datasets** where `fleet` and `ingest` put them by default: one level
  inside the collection (`--out-at-input`) and in the current directory. A dataset is matched to
  the collection by run id, so the other collections' datasets in the current directory are left
  out. Where a past `--out` wrote is not remembered: name it, or `--recurse`.

A fleet report starts with what the fleet itself recorded - and says who it could not read,
since a report that drops a failed host reads as a smaller, cleaner fleet:

```
fleet: 3 collection(s), 2 ingested, 1 failed  (.\.orca-fleet)
  c.lab: ingest failed: archive is truncated
  coverage gaps:
    DOM-DC-W2008.dom2008.lab: missing AD_computers, missing DNS_records, ... (10)
```

`--one-line` gives a failed host its line too (`c.lab: ok=False ingest=failed`), so counting lines
counts hosts; `--json` gives it an object (`"ingest": "failed"`, `"error"`) and every host its
`"ingest"` and `"coverage_gaps"`, still one flat list. A host a stale or foreign record calls
missing but whose run is reported anyway is not listed twice.

A sealed Outcome is found by the name ORC gives it, so discovery needs no key; reading it needs
one. Only an Outcome makes a directory a collection - a stray sealed log says nothing about what
a directory is.

**Logs are attributed per run.** ORC names a run's files after the run
(`ORC_<set>_<host>_<stamp>.log` beside `ORC_<set>_<host>_<stamp>_Outcome.json`), so in a shared
directory each collection reads its own log instead of all of them. In a dataset of its
own every log belongs to the one run, which is what makes the expanded `Errors` container and
ORC's console output show up where they should.

**Archives nothing accounts for are named**, once per directory, after the hosts:

```
\\host\archive\merged
  archives no Outcome names (1)
    ORC_WS-0009_Orphan.7z  32 bytes
```

In a merged dump that is the thing worth noticing: a collection whose Outcome never arrived, so
data is sitting there that nothing in the report says a word about. Matched by archive stem, so
the decrypted copy of a sealed archive is not mistaken for an orphan - and against every Outcome
the report read, not only the directory's own: `fleet` leaves the archives it decrypted in its
output directory while their Outcomes sit one level down in each host's dataset.

#### The default report

A header line naming the host and where it came from, the run's scalars, then one counted
section per thing worth looking at, every row of it in columns:

```
DOM-DC-W2008.dom2008.lab  .\ORC_DomainController_DOM-DC-W2008.dom2008.lab_20261006_120552
  ok=False  cmds=41  fails=16  dur=29m33s  mem=629.2MB (ORC_General)

  failed commands (16)
    ORC_General  Flashback              -            0m5s  cpu=0m0s  read=276.0B  wrote= 40.9KB  0x80070006
    ORC_General  Azure_Identity         -            0m0s  cpu=   ?  read=     ?  wrote=      ?  0xFFFFFFFF
    ORC_General  Autoruns               -            0m3s  cpu=0m0s  read=  0.0B  wrote=   0.0B  0xC0000139
    ORC_General  GetSamples             GetSamples  0m10s  cpu=0m1s  read= 7.5KB  wrote=748.8KB  0x80004005
    ORC_General  Processes1             -            0m0s  cpu=   ?  read=     ?  wrote=      ?  0xFFFFFFFF

  tables (16, 389566 rows, 15.7MB)
    NTFSInfo_00000000_DiskInterface_0x4e106ff4106fe187_    8.6MB  100576 rows  0.76s
    I30Info_00000000_DiskInterface_0x4e106ff4106fe187_     5.9MB  217202 rows  0.54s
    USNInfo_00000000_DiskInterface_0x4e106ff4106fe187_   684.5KB   66876 rows  0.37s
    GetThis_Residents                                    365.5KB    3008 rows  0.22s

  empty tables (3)
    VSS_list
    autoruns
    volstats_FatInfo_detail
  empty outputs (7)
    Residents.log
    autoruns.csv

  logs with errors (27 of 30)
    ORC_DomainController_DOM-DC-W2008.dom2008.lab_20261006_120552.log   1W  32E  0C
    GetSamples.dev.log                                                  8W   6E  1C

  unread tables (1)
    autoruns  duckdb failed: Invalid Input Error: Error when sniffing file "C:/Users/...

  critical (2)
    GetSamples.dev.log  12:20:10  Command failed with 0x80004005: Unspecified error
    GetSamples.log      12:20:10  Command failed with 0x80004005: Unspecified error
```

The real run continues from there: all 16 failures, all 16 tables, all 7 empty outputs and all
27 logs are printed, and the sample above is shortened for the README, not by `report`. **Nothing is
folded away.** A report that hid rows behind a `... N more` would be one you have to run again
with another flag to finish reading; the count in each heading is there to tell you how long
the list is, not to excuse not printing it. One real host comes to 95 lines.

A failure carries the columns a `--list` line does - set, command, tool, duration, exit code
last - so the two read the same way. The exit code is 32-bit hex, the way ORC's own logs and
Windows write it (`0x80004005`, not `2147500037`), so it can be looked up. Between the duration
and the code, what the process did: CPU time (`cpu=`, user plus kernel, which ORC records in
whole seconds) and the bytes it read and wrote. They tell failures apart that the code alone
does not: `cpu=?` and nothing read is a command that never started, while `0x80004004` after
exactly its timeout with a megabyte read is one that was cut short. `?` is a field the Outcome
did not record. Memory is on the scalars line only (`mem=`): an Outcome records a peak per
command set, not per command. `--json` keeps the code as ORC wrote it, plus `cpu_seconds`,
`read_bytes` and `write_bytes` on each failure. The `tables` section is one list of every table with its
size, rows and time, replacing the old `big` and `slow` pair, which on that run named the same
four tables twice.

**A section with nothing in it is left out.** The scalars already say `fails=0`, and a healthy
run should read as a short report rather than a page of zeros. Naming a view does print its
heading even when it is empty (`--view empty` on a clean run says `empty tables (0)`), since
asking for a view and getting nothing back cannot be told from a broken report.

#### The views, and what `--view` takes

`outcome`, `stats`, `empty` and `log` are what ran, what it produced, and which logs carried
something; `warn`, `error` and `critical` are what was actually said at that level. The names
are singular, a comma list and a repeated flag are the same thing, and `all` is every view:
`--view critical,error` is `--view critical --view error`. An unknown name is refused rather
than passed over, because a view that silently matched nothing prints a host header and reads
as a clean run.

The default report - no `--view` at all - is `outcome`, `stats`, `empty`, `log` and
`critical`. `warn` and `error` are left out of it on purpose: a run with thousands of
warnings would bury the rest of its own report. Ask for them by name, which is also how you
get at them on the usual run where nothing was critical but plenty went wrong.

#### Levels come from the log, not from the words in it

ORC writes every line through one pattern, `2026-10-03T08:15:03.000Z [E] ...`, so a log is
counted by its level tag: `12W  2397E  1C`. Counting lines that contain "error" or "failed"
instead is wrong in both directions - ORC logs `Failed to ...` at Debug all over the place
while reporting the failure once at Error. A log with no ORC line in it, a crashing tool's
stderr say, still gets the old substring count, marked `~3E` so it is not read as exact; a
log that could not be opened reads `unreadable` rather than counting `-1`.

A message can be preceded by a replay of the last 64 lines, between `--- BACKTRACE START ---`
and `--- BACKTRACE END ---`. **Before**, not after: ORC dumps the ring and then writes the line
that triggered the dump, so the block belongs to the message that follows it. Those lines are a
*replay* of lines already logged, with their original levels and timestamps, so they are
counted nowhere, and nothing inside a block is read as a message of its own: counting the block
would report every error inside the ring a second time.

They are part of a message's **context** though, and they have to be. The file facility sits at
Info on a real collection, while `dump_to` drops every sink to trace for the replay, so the
ring is the only place the debug lines that say *why* a failure happened are written at all -
on one real collection log, 216 of its 217 debug lines. What the context does drop is a replay
of the message itself, since ORC dumps the ring just before the *next* error and this one is in
it by then, and a line repeated back to back.

#### `--view warn|error|critical`: what was actually said

The log view says which log to open. These say why, in time order across every log of the run,
because a host's collection log and its per-command logs each hold part of one story:

```
  critical (2)
    ORC_HOST.log     09:14:22  Failed to open volume \\.\C: [access denied]
    USNInfo.dev.log  09:16:03  Job was authoritatively terminated
```

`--backtrace` prints the log lines either side of each message of whichever levels are being
shown - five of them by default, `--backtrace N` for N - ruled off above and below so the
contexts of several messages do not run into one wall of indented lines. The message itself is
marked with `>`, and carries its text in full where the row above it has had to cut it to the
width of the report:

```
  error (131)
    ORC_HOST.log  16:53:45  Failed CreateFile for 'C:\Windows\Temp\orc-temp\WorkingTe...
      ------------------------------
        16:53:45 [I] Upload  Add file: ORC_..._Outline.json.7z.p7b (5 KB)
        16:53:45 [D] capsule: Created
        16:53:45 [D] CommandAgent: Execute command 'VSS_list'
      > 16:53:45 [E] Failed CreateFile for 'C:\Windows\Temp\orc-temp\WorkingTemp\3308.tmp\orc_x64-xp.exe' [0x80070020: The process cannot access the file because it is being used by another process.]
        16:53:45 [D] Failed to open: 'C:\Windows\Temp\orc-temp\WorkingTemp\3308.tmp\orc_x64-xp.exe' [0x80070020: ...]
        16:53:45 [E] Failed to compute sha1 for command 'VSS_list' [0x80070020: ...]
        16:53:45 [D] VSS_list: Created
      ------------------------------
```

The context comes from the log, not from ORC's backtrace block, because the block on its own
is not the context: `dump_to` **clears** the ring at every dump, so with a trigger of `Error`
it holds the two or three lines logged since the previous error - on one real collection, 16
of its 32 blocks are two lines long and the first of the two is the previous error repeated -
while the three 64-line blocks are whatever happened to be logged during a quiet stretch
rather than anything to do with the error below them. The lines that actually surround a
failure are above it in the file. Block lines are part of that context (the file sits at Info,
so they are the only copy of the debug lines that say why), minus the replay of the message
itself and any line repeated back to back.

`--view error --backtrace` is what to reach for when the log's trigger was `Error` and nothing
was critical. It is off by default, and the lines are not even kept without it: five lines
either side of every message, over a fleet of logs, is most of the memory a scan takes. The
count bounds the scan as well as the printing.

#### The per-command logs are inside the archives

WolfLauncher's own log sits beside the archives; each command's `<Command>.dev.log` is *in*
one, and ORC's failing commands put theirs in an `Errors` container in there too. So a report
on a collection as it arrives used to read the collection log, find it clean, and print no
logs section at all - while the critical explaining the failure it had just listed sat in the
archive next to it. `report` now pulls those logs out before building the views:

```
  failed commands (1)
    ORC_General  NTFSInfo  -  0m0s  cpu=?  read=?  wrote=?  0x00000001

  logs with errors (1 of 2)
    NTFSInfo.dev.log  0W  1E  1C

  critical (1)
    NTFSInfo.dev.log  09:14:22  Failed to open volume \.\C: [access denied]
```

**It costs one decode per archive, and only the first time.** ORC writes a single solid block
(`Archive7z.cpp`), so reaching an entry means decoding everything in front of it - measured
here at ~250 MB/s of uncompressed content, 1.4 s for a 244 MB archive whose logs sit at the
very end. That is why it is one `7z` call per archive asking for every log at once rather than
a call per log, which would pay that again for each; listing first, to name the `Errors`
container for the same call, is free by comparison because it only reads the header. The logs
land in the work dir beside the unsealed sidecars, under a directory per run, and the next
report reuses them (`--refresh` to redo). `--no-archive-logs` skips the whole thing.

**A sealed archive is named, not opened.** The envelope is CMS over the whole archive, so one
log out of it costs decrypting all of it, which is an extract wearing a report's clothes. The
report says which archives it did not read and what would read them, rather than leaving the
gap invisible:

```
more in 1 sealed archive: ORC_HOST_General.7z.p7b (orca ingest <dir>)
```

A signpost, not an explanation: one line however many hosts are sealed.

`ingest` rather than `extract`, and the distinction is not cosmetic: `extract` expands into a
subdirectory per archive, which `run_log_files` deliberately does not walk, so a report on an
extracted tree still shows none of them. `ingest --out-at-input` leaves the decrypted `.7z`
*beside* the collection, which is all this path needs - the next report reads the logs out of it
with no key and no second decrypt - and it does not need duckdb to get that far. Hence the
`--out-at-input` in the signpost: a default ingest keeps that copy in its current directory,
where a report of the collection never looks.

Unsealing in `report` itself was measured and rejected. The decrypt is cheap: 0.27 s for a
98 MB sealed archive, against 1.09 s to pull the logs out of the 65 MB plain one it becomes,
so it is a fifth of a cost already being paid. It cannot be narrowed to the log entries
either - a 7z keeps its header at the end, so nothing can say where an entry is, or that one
exists, before the whole plain archive is on disk. That last point is the objection: `report`
would write a full-size copy of every archive, and without `--work-dir` rewrite it every run.
`ingest` writes that same copy once, and everything afterwards reuses it.

**A protected archive usually arrives with its own password.** ORC archives each
sub-command's `/config` next to that command's output as `<name>_cli_config.xml`
(`CommandAgent.cpp`), and a password-protected archive's password is in it, so nothing has to
be told: `report` reads it from beside the archive. `ingest`, `extract` and `check` take an
explicit `--password`, which overrides it; `report` has no such flag and does not need one.
An archive that still will not open is named rather than quietly yielding no logs, which would
look exactly like an archive holding none.

**A log seen twice is one log.** `ingest` leaves the per-command logs beside the tables, and a
report on that dataset also finds the archives still sitting there - so a log is keyed by its
path within the place it was found (`Errors/NTFSInfo.dev.log` and `NTFSInfo.dev.log` stay two
logs) and the run directory's copy is the one kept.

#### A collection as it arrives, with everything sealed

```powershell
# no ingest, no extract: the Outcome, the Outlines and the log are unsealed and read
orca report \\host\dumps\ORC_HOST --private-key contoso.com.key

# a whole fleet of them, failures only
orca report \\host\dumps --list --failed --private-key contoso.com.key

# and the row tables read the same sealed sidecars
orca report outline \\host\dumps --private-key contoso.com.key --json
```

When ORC encrypts a collection it seals its own outputs too, one `.7z.p7b` per file, so a
collection arrives holding `ORC_..._Outcome.json.7z.p7b`, an Outline and WolfLauncher's log in
the same state. Without reading them a report can say almost nothing - not the machine's name
(it would be reported under the *folder* name), not which command failed, not what the log
said. `report` now unseals what it needs, which makes a sealed collection reportable before
anything has been ingested or extracted:

```
DESKTOP-U9N26B0  //host/dumps/ORC_HOST
  ok=False  cmds=35  fails=12  dur=6m47s  mem=1.2GB (ORC_General)

  failed commands (12)
    ORC_General  USNInfo                   USNInfo   0m22s  0x00000012
    ORC_General  NTFSInfo_Details_Current  NTFSInfo  5m10s  0x00000009
    ORC_General  Autoruns                  -          0m0s  0x00000001

  tables: (no ingest timing: not a dataset)

  empty tables: (no ingest timing: not a dataset)

  logs with errors (1 of 1)
    ORC_..._144127.log  106
```

**Into the work dir, never beside the collection.** `ingest` and `extract` keep what they
unseal, so the next command finds plain files and nothing decrypts twice; a report only reads,
and what it reads has to stay as it was found - the collection directory is often read-only
anyway. `--work-dir` keeps the unsealed sidecars (under `<collection>-<digest>`, since two
machines' collections routinely share a folder name) and the next report reuses them;
`--refresh` unseals again; without a work dir they go to scratch and vanish with the process.
They are a few KB, so paying for the decryption again costs nothing.

`7z.exe` is needed only when sidecars are sealed, and nothing becomes fatal: if it is missing,
or no key or certificate can open them, each failure is named and the report prints the views
it can still build.

**A directory means every collection in it**, and `--recurse` means every collection under it.
There is no `--root`: a directory is read for what it holds (see *What a directory means*
below).

#### `report` never ingests

`report` is fast - under a second on a sealed two-machine share - and that is the point of it:
the question "is this fleet healthy" should not turn into a fleet ingest. So where a view needs
a dataset it was not given, it names the command rather than running it:

```
  tables: (not ingested; orca ingest \\host\archive\merged)
  empty tables: (not ingested; orca ingest \\host\archive\merged)
```

The outcome, the failures, `--list`, the log view and the message views all read ORC's own
sidecars, so they work on a collection as it arrives; only the two views built from `.orca`
need the ingest. This
is the opposite choice from `merge` and `diff` on purpose: for them the ingest *is* the work,
and for `report` it would be a surprise.

#### `--ignore`, `--ignore-file`: the findings you have already decided about

```powershell
orca report \\host\archive\merged --private-key contoso.com.key --ignore-file ignore.json
orca report --list --failed --ignore "PersistenceSniper,Azure*" --ignore ADComputers,DNS_records
```

A fleet has findings you already know about - a command that always fails on these machines, an
output ORC always writes empty - and a report that repeats them every time trains you to skim
past the ones that matter. `--ignore` takes names on the command line, repeated or
comma-separated; `--ignore-file` takes the same names as one JSON list, for a set you keep; both
add up. `*` and `?` work, and case does not matter, since ORC's own names mix cases (the
`Autoruns` command writes an `autoruns` table):

```json
[
  "Azure_*",
  "Autoruns",
  "EventConsumer.log",
  "processes?.log",
  "Capsule#*",
  "*.dev.log"
]
```

A name is matched against **every named finding**: a failed command, an empty output, an empty
table, an unread table, a log with errors, the lines of `--list`, and a fleet's coverage gaps. A
failed command can also be given as `set/command` (`ORC_Little/Autoruns`), so a name that means
one thing in one command set can be passed over without silencing it everywhere, or by its key
alone (`ORC_General`, the command set as `/key=` names it) to pass over every command of that
key. A command and the table it writes may be named differently: `ADComputers` writes
`AD_computers`, so silencing both takes both names.

A fleet's coverage gaps say what they passed over the same way, and a host whose every gap was
ignored keeps its line:

```
  coverage gaps:
    DOM-DC-W2008.dom2008.lab: missing EventConsumer, missing FatInfo, ... (7 of 10, 3 ignored)
    DOM-DC-W2008R2.dom2008r2.lab: missing FatInfo (1 of 3, 2 ignored)
```

**Nothing is hidden.** Each section heading says what it kept of how many and how many it passed
over, `--one-line` and the JSON carry an `ignored` total, and a section whose every row was
ignored still prints its heading rather than vanishing - the heading is the only record of the
choice in a human report:

```
DESKTOP-U9N26B0: ok=False cmds=35 fails=7 empty=2 tables=26 ... ignored=33

  failed commands (7 of 12, 5 ignored)
    ORC_General  USNInfo                   USNInfo   0m22s  0x00000012
    ...
  empty outputs (0 of 9, 9 ignored)

  logs with errors (15 of 35, 18 ignored)
    USNInfo.log  124
    ...
```

A run whose every failure is ignored reads as `ok=True`: that is what the list is for. On the
real two-machine share, five patterns took 24 failures down to the four nobody expected:

```
without:  DESKTOP-U9N26B0: ok=False cmds=35 fails=12 ...
          w11test:         ok=False cmds=35 fails=12 ...
with:     DESKTOP-U9N26B0: ok=False cmds=35 fails=2 ... ignored=10
          w11test:         ok=False cmds=35 fails=2 ... ignored=10

orca report ... --ignore-file ignore.json --list --failed
  DESKTOP-U9N26B0  ORC_General  USNInfo                   USNInfo   0m36s  0x00000012
  DESKTOP-U9N26B0  ORC_General  NTFSInfo_Details_Current  NTFSInfo  6m14s  0x00000009
  w11test          ORC_General  USNInfo                   USNInfo   0m23s  0x00000012
  w11test          ORC_General  NTFSInfo_Details_Current  NTFSInfo  5m39s  0x00000009

  50 command(s), 4 failed, 20 ignored, 2 host(s)
```

The file is read before anything is decrypted, and a file that is there but unreadable is an
error naming it: an ignore file with a typo that silently ignored nothing would make a report
look clean for the wrong reason. `report outcome` and `report outline` do not take `--ignore` or
`--ignore-file` -
they write a queryable table, and a row you want gone belongs in the query, not in the table.

#### `--one-line`: one line per host

```
DESKTOP-U9N26B0: ok=False cmds=35 fails=12 empty=2 tables=26 size=64.4MB dur=6m47s mem=1.2GB empty_files=9 unread=1
```

`cmds` is the denominator of `fails`: 12 failures out of 35 commands and 12 out of 300 are not
the same collection, and the line used to read identically either way. `empty` and `tables`
come from the ingest, `size` is the ingested parquet total, `dur` and `mem` from the Outcome.

`empty_files` and `unread` appear **only when they are not zero**, because a field that always
reads zero stops being read, and each says something out of the ordinary: `empty_files` that
ORC wrote an output and put nothing in it, `unread` that a table could not be ingested at all.
Neither appears when its input could not be read either - an unread manifest must not come out
as "no empty files". On the collection above, the nine empty outputs are the four
`Azure_*.json` whose commands exited 1, four empty logs and the Capsule marker, and the one
unread table is the `autoruns.csv` that is really UTF-16 usage text.

#### `--list`: one line per command, across the fleet

The view to scan when you have N machines and expect none of them to have failed. One line per
command, no per-host headers, machine first and exit code last:

```
DESKTOP-U9N26B0  ORC_General  USNInfo                   USNInfo   0m22s  1.2GB  0x00000012
DESKTOP-U9N26B0  ORC_General  NTFSInfo_Details_Current  NTFSInfo  5m10s  1.2GB  0x00000009
DESKTOP-U9N26B0  ORC_General  GetThis_Default           GetThis   0m45s  1.2GB  0x00000009
DESKTOP-U9N26B0  ORC_General  Autoruns                  -          0m0s  1.2GB  0x00000001
TESTHOST         ORC_General  NTFSInfo                  NTFSInfo   1m2s  412MB  0x00000000

  35 command(s), 12 failed, 1 host(s)
```

Lines run by host, then in the order ORC ran them. `peak` is the command **set's** peak memory
repeated across its commands, because that is the only memory an Outcome records, and `?` where
it records none; `tool` is `-` for a command that runs a packaged binary rather than an ORC
subtool, which the Outcome leaves null. `--failed` keeps a line only when the exit code is
non-zero *or unparseable* - a code ORC wrote that is not a number is a failure too - and the
summary still counts every command, since a filtered `12 failed` with no denominator says
nothing. On a clean fleet, `--list --failed` prints the summary and nothing else.

The rows are the same ones `orca report outcome` writes to parquet, so the text and the table
cannot disagree, and `--list` needs no duckdb. A dataset with no Outcome does not abort the
sweep: its commands are simply not there and it is named in a trailing note, because one
machine missing an Outcome must not cost you the other eight.

#### A view says so when its input is missing

`report` takes an *ingested* dataset, but ORC's own files are also in a directory `extract`
produced, so it is pointed at one of those often enough. Most views can be built from an
extracted tree - `outcome`, `log` and the three message views read ORC's files; `stats` and
`empty` read `.orca\ingest-timing.csv` and `.orca\manifest.parquet`, which only `ingest`
writes:

```
DESKTOP-U9N26B0  //host/dumps/ORC_HOST
  ok=False  cmds=35  fails=12  dur=6m47s  mem=1.2GB (ORC_General)

  failed commands (12)
    ORC_General  USNInfo                   USNInfo   0m22s  0x00000012
    ORC_General  NTFSInfo_Details_Current  NTFSInfo  5m10s  0x00000009
    ORC_General  Autoruns                  -          0m0s  0x00000001

  tables: (no ingest timing: not a dataset)

  empty tables: (no ingest timing: not a dataset)

  logs with errors (1 of 1)
    ORC_..._144127.log  106
```

That used to read `empty: 0 table(s) -> (none)` and `stats: 0 tables, 0 rows, 0.0B`, which is
indistinguishable from a genuine finding - a missing input reading as good news is the one
thing a health report must not do. `--one-line` prints `?` for those fields and the JSON
carries `null`, never 0. The two halves of `empty` are tracked separately: a run ingested
without duckdb has real timing and no manifest, and one flag would relabel one missing input as
the other.

#### `--json`: one flat object per host

Every `--one-line` scalar at the top level, each view's detail on a key below it, and an
`*_available` flag per input so a filter can tell 0 from unknown. Flat on purpose - filtering a
fleet has to stay a one-liner:

```powershell
orca report E:\datasets --json | ConvertFrom-Json | ? fails -gt 0 | Select host,fails,tables

host               ok fails tables stats_available
----               -- ----- ------ ---------------
DESKTOP-U9N26B0 False    12                  False

# and the detail is there once the filter has found something
orca report E:\datasets --json | ConvertFrom-Json |
    % { $_.failures } | Group-Object exit_code | Select Name,Count
```

Keys below the scalars: `failures`, `empty_tables`, `empty_files`, `biggest`, `slowest`,
`logs`, `warn_messages`, `error_messages`, `critical_messages`, `skipped`. The scalar
`criticals` is the count a filter reads; the messages, each with its log and - with
`--backtrace` - the lines either side of it under `before` and `after`, are under the three
`*_messages` keys.

### Diff two versions

```powershell
# straight from the collected archives: each side is ingested once and cached in the current
# directory (see "A reference you compare against again and again" below)
orca diff E:\dumps\10.3.3\DFIR-ORC_HOST.7z E:\dumps\10.3.1\DFIR-ORC_HOST.7z

# encrypted results, and a result directory (Outcome json + top archives) on either side
orca diff --run E:\dumps\10.3.3 --reference E:\dumps\10.3.1 --private-key contoso.com.key

# compare two whole runs: file-set + CRC, then per-table rows (volatile columns auto-dropped)
orca diff --run E:\datasets\host-10.3.3 --reference E:\datasets\host-10.3.1

# preview up to 20 differing rows per changed table (readable, column-aware)
orca diff --run E:\datasets\host-10.3.3 --reference E:\datasets\host-10.3.1 --show

# just two files
orca diff E:\datasets\host-10.3.3\getthis.parquet E:\datasets\host-10.3.1\getthis.parquet

# focus on file identity only (ignore per-run volatile metadata)
orca diff A\getthis.parquet B\getthis.parquet --columns FullName SHA1 SizeInBytes --show

# export the differing rows to CSV instead of (or as well as) previewing
orca diff --run NEW --reference REF --rows --out-dir E:\datasets\diff

# ignore run-local files in the file-set layer (logs, per-run markers)
orca diff --run NEW --reference REF --exclude-files "*.log" "*/Statistics.json" "Capsule#*"

# pair rows on your own identity columns, and widen the change-pattern list
orca diff --run NEW --reference REF --show --key FRN AttrName --patterns 30

# summary shapes
orca diff --run NEW --reference REF --one-line
orca diff --run NEW --reference REF --json
```

**A reference you compare against again and again.** A side given as a dataset is used in
place. A side given as an archive or a collection is ingested into the current directory and
reused only when `diff` runs from that same directory again. Run each new build's diff from a
directory of its own and the reference - unchanged, and usually the bigger side - is decrypted
and ingested again every time. Three ways to pay for it once:

```powershell
# 1. ingest the reference once, then hand diff the dataset: never re-ingested, from anywhere
orca ingest E:\dumps\10.3.1 --private-key contoso.com.key --out E:\refs\host-10.3.1
orca diff --run E:\dumps\10.3.3 --reference E:\refs\host-10.3.1 --private-key contoso.com.key

# 2. keep each side's dataset beside its input, where any later diff finds it
orca diff --run E:\dumps\10.3.3 --reference E:\dumps\10.3.1 --private-key contoso.com.key --out-at-input

# 3. one fixed place for every diff's datasets, whatever the current directory
orca diff --run E:\dumps\10.3.3 --reference E:\dumps\10.3.1 --private-key contoso.com.key --work-dir E:\orca-work
```

The first is the one to prefer: the reference becomes an explicit, named dataset, and nothing is
written beside the evidence. `--out-at-input` applies to both sides, so the new build's dataset
lands beside its archive too. Under `--work-dir` the datasets are named after their input's
location, so the same reference archive maps to the same dataset on every call.

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
orca ingest E:\dumps\HOST.p7b --out E:\datasets\host --private-key contoso.com.key
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

### Check (is this collection intact?)

```powershell
# every layer: the Outcome's sha1, the .p7b envelope, then every container
orca check E:\dumps\DFIR-ORC_HOST.7z.p7b --private-key contoso.com.key

# a whole collection directory, and a work dir to keep the decrypted archive in
orca check \\host\share\ORC_WS_H2ODEV_20260921_172528 --private-key contoso.com.key `
    --work-dir E:\orca-work

# just the envelope (decrypts, replays the journal, stops)
orca check E:\dumps\HOST.7z.p7b --private-key contoso.com.key --depth 1

# summary shapes, and the exit code is what a pipeline reads
orca check E:\dumps\HOST.7z --one-line
orca check E:\dumps\HOST.7z --json
```

`check` answers one question - *is this collection still what ORC wrote?* - and answers it
**without expanding the tree**. `extract` on the real collection writes 1.6 GB; `check` on the
same one peaks at the decrypted archive plus **one container at a time**, and verifies more than
a listing can, because it decompresses every entry and recomputes its CRC.

Three layers, cheapest first:

| layer | what it proves | cost |
|:---|:---|:---|
| Outcome | the file ORC recorded is on disk, at the recorded size, with the recorded sha1 | one read per top archive |
| envelope | the CMS envelope decrypts and the journal replays into a real archive | the decrypt (kept and reused) |
| container | every entry of every archive decompresses and its CRC still matches | CPU, one container on disk at a time |

The sha1 layer matters because it is the only one that can see a **truncated** collection: a
short archive whose headers are gone is unopenable, but a short archive can also be internally
consistent, and then `7z t` has nothing to complain about. ORC records the sha1 of the file it
wrote - for an encrypted run the hash stream sits below the CMS encoder, so it is the `.p7b`'s
own hash - which is why this layer needs no key at all. Turn it off with `--no-outcome`.

On the real 158 MB encrypted collection, 14 checks in 8.5 s:

```
  ok  ORC_WorkStation_DESKTOP-U9N26B0_General.7z.p7b  sha1 ok, 158444878 bytes
  ok  ORC_WorkStation_DESKTOP-U9N26B0_General.7z      88 entries, 157808210 bytes
  ok    USNInfo.7z                                    2 entries, 3885063 bytes
  ok    GetThis_Protected.7z                          1655 entries, 46844872 bytes
  ok    Residents.7z                                  41222 entries, 10791279 bytes
  ...
  14 checked, 14 ok, 0 failed
```

and with one byte of those 157 MB flipped, `check` names the container it landed in and exits 1:

```
  FAILED  ORC_DAMAGED_General.7z  CRC Failed : GetThis_Default.7z

  1 checked, 0 ok, 1 failed
```

**The verdicts.** `ok`, and six ways to be unhappy: `FAILED` (7-Zip could not read an entry
through), `MISSING` (the Outcome names an archive the directory lacks), `SHORT` / `size differs`,
`sha1 differs`. Three more are reported without failing the run, because none of them means
anything was lost: `empty` (a 0-byte archive, which ORC does write, and which the Outcome agrees
is 0), `warnings` (7-Zip's exit code 1), and `unsealed` (only the decrypted copy is on disk, so
the Outcome's sha1 - taken over the sealed file - has nothing to compare against). The exit code
is 1 if anything failed, which is the point of the command in CI.

**Nothing is written to the collection.** `ingest --out-at-input` unseals sidecars beside the
archives on purpose, to be reused; a verification pass must leave the evidence as it found it,
so `check` has no such option and unseals what it needs into the work dir. `--work-dir` is a keep dir as it is
everywhere else: the decrypted archive lands there and the next `check`, `ingest` or `extract`
of the same collection reuses it rather than paying for the decrypt again.

**What it does not do.** An archive that fails is not recursed into - its containers would be
extracted out of an archive 7-Zip has just said it cannot read through, and the entry that
failed is already named. So a partially damaged top archive tells you *what* broke, not which of
its undamaged containers are still sound; run `extract` when you need to know that.

### Where orca writes

Everything `ingest`, `fleet`, `diff`, `merge` and `getthis` derive from a collection - the
dataset, the decrypted archives, the unsealed sidecars - lands in the **current directory**. A
collection is evidence, and by default not one file is written into it. (`extract` writes only
under its own `--out`, the current directory too, and `check` and `report` only read - see
above.)

`--out-at-input` puts all of it **beside the input** instead: inside a collection directory, or
beside an archive, so one place holds everything that came out of one collection.

```
E:\dumps\                                        with --out-at-input
  ORC_WS_H2ODEV_20260921_172528.log             sidecar log, unsealed in place
  ORC_WS_H2ODEV_20260921_172528_Outcome.json    sidecar, unsealed in place
  ORC_WS_H2ODEV_20260921_172528_Outline.json
  ORC_WS_H2ODEV_Offline.7z.p7b                  the collected archive
  ORC_WS_H2ODEV_Offline.7z                      decrypted, kept and reused
  ORC_WS_H2ODEV_Offline\                        the run (flat; see above)
    NTFSInfo_details.parquet  GetThis_Default.parquet  ...
    ORC_..._Outcome.json  ORC_..._Outline.json  ORC_..._172528.log  .orca\
```

Without it the same files are in the current directory, and `E:\dumps\` is left as it was.

**When `--out-at-input` is worth it.** The products are a cache - a decrypted archive is as
big as the collection, and decrypting and replaying a multi-GB collection is the expensive part
of an ingest - and a cache in the current directory is found again only from that directory.
Run from somewhere else, or by a colleague, and the collection is decrypted and ingested again.
Beside the input the products are found from anywhere: by any command, from any directory, and
by everyone reading a collection on a share. It is also what `report` needs to show a sealed
collection's logs, and what datasets ingested before this default changed were written as. It is
refused with `--work-dir`, and with `--out` on `ingest` and `fleet`, which already name a place.

A decrypted archive and an unsealed sidecar keep the encrypted file's own name with only the
extension changed (`X.7z.p7b` -> `X.7z`, `X_Outcome.json.7z.p7b` -> `X_Outcome.json`), and both
are reused on the next run.

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
orca ingest E:\dumps\ORC_WS_H2ODEV_Offline.7z                 # run lands in the current directory
orca ingest E:\dumps\ORC_WS_H2ODEV_Offline.7z --out-at-input  # run lands beside the archive
orca diff NEW.7z REF.7z --work-dir E:\orca-work               # put both runs somewhere with room
orca diff NEW.7z REF.7z --refresh                             # re-decrypt and re-ingest
```

`--work-dir` moves the runs, the decrypted output **and the scratch** somewhere else, all at
once - a collection larger than the temp volume cannot be decrypted otherwise. Under a shared
root - the current directory or `--work-dir` - a dataset the cache builds for `diff`, `merge`
or `getthis` is named after its input plus a digest of where that input is
(`DFIR-ORC_NEW-0154fef1aff2`): two collections' archives often share a basename, and a
re-collected archive at the same place rebuilds its own dataset instead of leaving a stale one
behind. `--out-dir` is unrelated: it is where `--rows` writes its CSVs.

A run is reused when its marker still matches the key of its sources - each archive's name,
size and mtime plus the ingest options, not a content hash, which would mean reading the whole
archive to decide whether to skip work. `--refresh` is the escape hatch. Rebuilding a run
removes only what the previous ingest recorded in `.orca\products.txt`, never the collection's
own files or an expanded tree sharing the directory.

Everything orca does *not* intend to keep - nested containers, the CSV being converted, the
CMS/journal intermediates - goes to `%TEMP%\orca\<pid>` and is removed when the process ends.
`--work-dir` moves that scratch to a volume with room, on every command that takes it. If the
place the products go cannot be written to (a read-only share or image under `--out-at-input`),
an ingest's products go to scratch too and are simply not reused.

What may be removed is decided by an **index**, never by what a directory is called: each run
writes one in `%TEMP%\orca\` naming the scratch it created, and a directory no index names is
left alone however it is named. The index stays in `%TEMP%` even when `--work-dir` puts the
scratch elsewhere, so a run killed mid-way is cleaned up by a later run that knows nothing
about that directory.

### Outline (one queryable row per ORC run)

A mode of `report`, since it answers a question about a run rather than about its data.
The Outline says what ran where: the exact ORC build, the machine, its OS, CPU and memory, the
command line, the configured archives. `ingest` captures every Outline a collection carries
(sealed ones too), and `orca report outline` turns them into a table.

```powershell
orca report outline E:\datasets --out E:\datasets\fleet.parquet   # across many collections
orca report outline E:\datasets\host                                  # one run -> <dataset>\outline.parquet
orca report outline E:\datasets\host --json                           # just print the rows
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

Also a mode of `report`. Where `report outline` says what ran, `outcome` says how it went - one row per command executed, with
its exit code, timing, IO counters and the archive it wrote into.

```powershell
orca report outcome E:\datasets --out E:\datasets\commands.parquet
orca report outcome E:\datasets\host            # one run -> <dataset>\outcome.parquet
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
orca merge E:\datasets\host

# lossless: keep every distinct value as a recency-ordered array (slower)
orca merge E:\datasets\host --lossless

# straight from a collection: it is ingested first, then merged
orca merge \\host\dumps\ORC_HOST.7z.p7b --private-key contoso.com.key
```

**It ingests what it is given if it is not a dataset yet.** There is nothing to ask: `merge`
reads per-table parquet and only `ingest` writes it, so an archive or a collection has exactly
one way to become mergeable. The run cache means the second `merge` of the same collection pays
nothing, and `diff` resolves its two sides the same way. It takes the flags an ingest needs -
`--private-key`, `--password`, `--cms-backend`, `--work-dir`, `--refresh`, `--strict` - and
`getthis` does all of this too.

On the real sealed collection, `merge` straight from the `.p7b` decrypts, ingests and merges in
11.8 s (455369 rows, 72 columns), skipping the one table duckdb will not read and naming it.
Under `--strict` it stops instead, and the last line of the failure says so, because an ingest
can also fail for a reason `--strict` had nothing to do with:

```
  that is what --strict asks for; without it the tables that do read are ingested
```

### Ad-hoc queries

Any parquet in a run is a normal DuckDB table:

```powershell
duckdb -c "SELECT column_name FROM (DESCRIBE SELECT * FROM 'E:/datasets/host/getthis.parquet')"
duckdb -c "SELECT FullName, SizeInBytes FROM 'E:/datasets/host/getthis.parquet' ORDER BY FullName LIMIT 50"
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
