# Querying a fleet database: examples

`orca fleet` writes `fleet.duckdb` beside the datasets: one view per table across every host, a
`<table>_typed` companion with real types, and orca's own record of the fleet (see the README,
"Fleet"). The views read `fleet\`, one parquet per table beside the database, by a relative path:
open it from its own directory, or `SET file_search_path` to it first.

```powershell
orca fleet E:\collect --private-key contoso.com.key
duckdb fleet.duckdb
```

Every query below was run against the three domain controllers of `S:\llm\dataset\sample`; the
comments say what it found there. Column names are ORC's (NTFSInfo, USNInfo, GetThis) or the
collected tool's own (autoruns, processes); `DESCRIBE <view>` lists them.

A few rules of thumb:

- Use the `_typed` views for dates and numbers (`BETWEEN`, `INTERVAL`, `max()`, arithmetic). A
  value that does not parse is NULL there; the plain view keeps exactly what was collected.
- `OrcComputerName`, `OrcRunId` and `OrcTimestamp` say which host and run a row came from;
  `OrcParquetFile` which file - for NTFSInfo or USNInfo, which volume.
- `COPY (SELECT ...) TO 'result.csv'` saves any result for a report.

## 1. Who answered, and what is missing

Read this before any count across the fleet: a host that did not produce a table is absent from
every query on it, and absent must not read as clean.

```sql
SELECT OrcComputerName, status FROM orca_hosts;
SELECT * FROM orca_coverage ORDER BY 1, 2;
```

On the sample: all three hosts ingested; W2008 has no `processes1/2`, `DNS_records`,
`EventConsumer`, ... - those commands did not run there.

`orca_tables` says what each view spans (hosts, files) and how its typed view was typed;
`orca_schema` where the types came from.

## 2. Stacking autoruns: what only one host has

The classic estate-wide hunt: an autostart entry present on one machine out of many. Unsigned
entries first.

```sql
SELECT "Image Path", min(Signer) AS signer, count(DISTINCT OrcComputerName) AS hosts,
       string_agg(DISTINCT OrcComputerName, ', ') AS seen_on
FROM autoruns_typed
WHERE "Image Path" <> '' AND "Image Path" NOT LIKE 'File not found%'
GROUP BY "Image Path" HAVING hosts = 1
ORDER BY bool_or(Signer LIKE '(Verified)%'), 1;
```

On the sample, first: `c:\windows\system32\drivers\cht4vx64.sys`, a Chelsio driver,
`(Not verified)`, on the RODC only.

## 3. Processes running from outside C:\Windows

```sql
SELECT OrcComputerName, ProcessName, ExecutablePath, CommandLine
FROM processes1_typed
WHERE ExecutablePath <> '' AND ExecutablePath NOT ILIKE 'C:\Windows\%';
```

On the sample: `C:\CmdExecService\cmdexsvc.exe` on two DCs, and ORC itself running from
`\\10.0.0.3\share`.

## 4. Executables created shortly before the collection

Relative to each host's own collection time, so hosts collected on different days compare.

```sql
SELECT OrcComputerName, CreationDate, SizeInBytes, ParentName || '\' || File AS path
FROM NTFSInfo_typed
WHERE Extension IN ('.exe', '.dll', '.ps1', '.bat')
  AND CreationDate >= strptime(OrcTimestamp, '%Y%m%d_%H%M%S') - INTERVAL 30 DAY
  AND ParentName NOT ILIKE '\Windows\Temp\WorkingTemp%'   -- ORC's own tools, unpacked to run
ORDER BY CreationDate DESC;
```

Without the last line, the sample shows ORC's tools (autorunsc, handle, Listdlls, ...) unpacked
into `\Windows\Temp\WorkingTemp\` minutes before the collection.

## 5. Timestomping hint: $STANDARD_INFORMATION older than $FILE_NAME

Tools that backdate a file usually change `$STANDARD_INFORMATION` only; `$FILE_NAME` keeps the
real creation time.

```sql
SELECT OrcComputerName, ParentName || '\' || File AS path,
       CreationDate AS si_created, FileNameCreationDate AS fn_created
FROM NTFSInfo_typed
WHERE CreationDate < FileNameCreationDate - INTERVAL 1 DAY
  AND Extension IN ('.exe', '.dll', '.sys')
  AND ParentName NOT ILIKE '\Windows\WinSxS%'   -- the component store keeps build timestamps
ORDER BY fn_created - CreationDate DESC;
```

A lead, not a finding: on the sample it returns W2008 drivers stamped 2008 and laid down in
2019 - the operating system's installation, not tampering.

## 6. USN journal: deleted executables

```sql
SELECT OrcComputerName, count(*) AS deletions, max(TimeStamp) AS last_one
FROM USNInfo_typed
WHERE Reason LIKE '%FILE_DELETE%' AND (File ILIKE '%.exe' OR File ILIKE '%.dll')
GROUP BY ALL;
```

On the sample: 553 deletions on the RODC against 20 and 55 on the others - worth listing which
files, and when:

```sql
SELECT TimeStamp, FullPath FROM USNInfo_typed
WHERE OrcComputerName LIKE 'APPS2012R2-RODC%' AND Reason LIKE '%FILE_DELETE%'
  AND (File ILIKE '%.exe' OR File ILIKE '%.dll')
ORDER BY TimeStamp DESC LIMIT 50;
```

On the sample the most recent are ORC's own: an earlier run cleaning up the tools it unpacked to
`\Windows\Temp\WorkingTemp\` (`Orc.exe`, `handle.exe`, ...). Add
`AND FullPath NOT ILIKE '\Windows\Temp\WorkingTemp%'` to look past them.

## 7. A hash across the fleet

Every collected sample, by how many hosts hold it - and the IOC check, against every host at once.

```sql
SELECT SHA1, any_value(SampleName) AS sample, count(DISTINCT OrcComputerName) AS hosts
FROM GetThis_GetThis_Default_typed WHERE SHA1 <> ''
GROUP BY ALL ORDER BY hosts;

SELECT OrcComputerName, FullName, SampleCollectionDate
FROM GetThis_GetThis_Default_typed WHERE SHA1 = '1F6B861DAB7AE474AA7FFEC33C963CDF3BF4A943';
```

The same works on the hashes autoruns records (`"SHA-256"`, `MD5`) and, when NTFSInfo was
configured to hash, on `NTFSInfo_typed.SHA1`.

## 8. WMI persistence: permanent event consumers

```sql
SELECT OrcComputerName, __CLASS, Name FROM EventConsumer_typed;
```

On the sample only the built-in `SCM Event Log Consumer`. A `CommandLineEventConsumer` or an
`ActiveScriptEventConsumer` would stand out.

## 9. One timeline across hosts

File system and journal events of every host, in one ordered list - here, around a moment of
interest.

```sql
SELECT TimeStamp AS event_time, OrcComputerName, 'usn ' || Reason AS what, FullPath AS path
FROM USNInfo_typed
WHERE TimeStamp BETWEEN TIMESTAMP '2026-10-06 09:00' AND TIMESTAMP '2026-10-06 10:00'
UNION ALL
SELECT CreationDate, OrcComputerName, 'created', ParentName || '\' || File
FROM NTFSInfo_typed
WHERE CreationDate BETWEEN TIMESTAMP '2026-10-06 09:00' AND TIMESTAMP '2026-10-06 10:00'
ORDER BY event_time;
```

Timestamps are UTC, as ORC writes them.
