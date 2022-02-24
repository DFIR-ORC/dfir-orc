# Continous Integration

This directory contains resources to help build and test DFIR-Orc with continuous integration. Currently those scripts are only used internally and Azure does not implement them.


## Resources

| File               | Description                                                                               |
|:-------------------|:------------------------------------------------------------------------------------------|
| build.ps1          | Script file to be sourced. Facilitate DFIR-Orc builds with 'Build-Orc' function.          |
| test.psm1          | Module with multiple functions to help building functional tests and check their results. |
| contoso.com.pem    | Public key for testing encryption (.p7b)                                                  |
| contoso.com.key    | Public key for testing decryption (.p7b)                                                  |



## The functional test module
The `test.psm1` module contains multiple functions to help building functional tests and check their results.


### Minimal requirements
- Powershell Core - https://github.com/PowerShell/PowerShell/releases
- 7zip - https://www.7-zip.org/download.html

### Invoke-OrcVM

>**Requirements:** an Hyper-V virtual machine with a checkpoint.

Configure, drop, execute DFIR-Orc in a virtual machine.

Credentials to access the virtual machine using remote powershell are required and can be cached with Get-Credential.

```
Invoke-OrcVM `
    -Path dfir-orc\configuration\orc.exe `
    -VMName 'Server 2016' `
    -Checkpoint wsus `
    -Upload \\192.168.0.1\orc_results `
    -PublicKey dfir-orc\configuration\test.pem `
    -Credential $Cred `
    -Argument "/overwrite" `
    -ErrorAction Stop
```


### Invoke-OrcOffline
Execute ORC on a disk dump or a virtual hard disk.

```
Invoke-OrcOffline `
    -Path dfir-orc\configuration\orc.exe `
    -Disk w10.vhdx `
    -Destination results\w10 `
    -PublicKey dfir-orc\configuration\test.pem `
    -ErrorAction Stop
```


### Test-OrcResults

>**Requirements:** To enable decryption support and .p7b files (`-PrivateKey` switch):
>- Openssl (can use included version from https://gitforwindows.org)
>- Compiled unstream in the script's directory - https://github.com/DFIR-ORC/orc-decrypt/blob/master/README.md#cmake-build-instructions-windowslinux

Check one or multiple DFIR-Orc results using Outcome's exit codes and do some basic integrity tests.

This command expands and test results using `Expand-OrcResults` and `Test-OrcExpandedResults` which can be called directly.

```
Test-OrcResults results -Destination out -PrivateKey dfir-orc\configuration\test.key
```

Output example
```
WARNING: WIN-UEP1AAROS8L: Failed ORC_Powershell/Azure_Identity (code: 0x1, duration: 00.00:00:21, start: 11:39:07 AM)
WARNING: WIN-UEP1AAROS8L: Failed ORC_Powershell/DNS_records (code: 0x1, duration: 00.00:00:02, start: 11:38:43 AM)
WARNING: WIN-UEP1AAROS8L: Failed ORC_Powershell/Azure_ScheduledEvents (code: 0x1, duration: 00.00:00:22, start: 11:39:06 AM)
WARNING: WIN-UEP1AAROS8L: Failed ORC_Powershell/Azure_Attested (code: 0x1, duration: 00.00:00:21, start: 11:38:45 AM)
WARNING: WIN-UEP1AAROS8L: Failed ORC_Powershell/Azure_Instance (code: 0x1, duration: 00.00:00:21, start: 11:38:46 AM)
WARNING: WIN-UEP1AAROS8L: Failed ORC_General/GetScripts (code: 0x80070057, duration: 00.00:00:00, start: 11:30:21 AM)
WARNING: WIN-UEP1AAROS8L: Empty file: Residents.log
WARNING: WIN-UEP1AAROS8L: Empty file: WFP_dumper.log
WARNING: WIN-UEP1AAROS8L: Empty file: Azure_Attested.json
WARNING: WIN-UEP1AAROS8L: Empty file: Azure_Identity.json
WARNING: WIN-UEP1AAROS8L: Empty file: Azure_Instance.json
WARNING: WIN-UEP1AAROS8L: Empty file: Azure_ScheduledEvents.json
WARNING: WIN-UEP1AAROS8L: Empty file: DNS_records.txt
WARNING: WIN-UEP1AAROS8L: Empty file: EventConsumer.log
WARNING: WIN-UEP1AAROS8L: Empty file: processes1.log
WARNING: WIN-UEP1AAROS8L: Empty file: processes2.log
```


### ConvertTo-OrcDiffableResults

>**Requirements:** Nushell v0.35 - https://github.com/nushell/nushell/releases/tag/0.35.0

Convert DFIR-Orc offline results so they can be compared between multiple execution.

It will filter files, sort and strip csv so a comparison between two different DFIR-Orc version will be easier.
It can be used with continuous integration for 'DFIR-Orc offline' mode but currently requires human analysis for 'live' results.

```
ConvertTo-OrcDiffableResults `
    -Path w10_enterprise_offline\10.1.0-rc10\out\ `
    -Destination w10_enterprise_offline\10.1.0-rc10\diffable\ `
    -Exclude `
        "*.log", `
        "config.xml", `
        "volstats.csv", `
        "JobStatistics.csv", `
        "ProcessStatistics.csv", `
        "*_Outline.json", `
        "*_Outcome.json", `
        "GetSectors.csv"
```


### Compare-OrcDiffableResults

>**Requirements:** NuShell v0.35 - https://github.com/nushell/nushell/releases/tag/0.35.0

Compare two differents set of DFIR-Orc results that have already been prepared for the diff with `ConvertTo-OrcDiffableResults`.

Archives made on a running system will still need some "human analysis", use `ConverTo-OrcDiffableResults`.

```
Compare-OrcDiffableResults `
    -Path w10_enterprise_offline\10.1.0-rc10\diffable\ `
    -Reference w10_enterprise_offline\10.1.0-rc9\diffable\ `
    -Exclude `
        "*.log", `
        "config.xml", `
        "volstats.csv", `
        "JobStatistics.csv", `
        "ProcessStatistics.csv", `
        "*_Outline.json", `
        "*_Outcome.json", `
        "GetSectors.csv"
```

Output example
```
02:16:15 Statistics for 'C:\temp\orc\w10_enterprise_offline\build\diffable\'
DANDEV: Execution: 00.00:02:19, fails: 0, max memory: 695.6 MB (ORC_Offline), files: 42612 (756.9 MB)
02:16:18 Statistics for 'C:\temp\orc\w10_enterprise_offline\10.1.0-rc9\diffable\'
DANDEV: Execution: 00.00:02:16, fails: 0, max memory: 695.2 MB (ORC_Offline), files: 42612 (782.3 MB)
WARNING: Files differ: 'DANDEV\ORC_WorkStation_DANDEV_Offline\ADS\GetThis.csv'
WARNING: Files differ: 'DANDEV\ORC_WorkStation_DANDEV_Offline\Artefacts\GetThis.csv'
WARNING: Files differ: 'DANDEV\ORC_WorkStation_DANDEV_Offline\Browsers_complet\GetThis.csv'
WARNING: Files differ: 'DANDEV\ORC_WorkStation_DANDEV_Offline\CatRoot\GetThis.csv'
WARNING: Files differ: 'DANDEV\ORC_WorkStation_DANDEV_Offline\Event\GetThis.csv'
WARNING: Files differ: 'DANDEV\ORC_WorkStation_DANDEV_Offline\EXE_TMP\GetThis.csv'
WARNING: Files differ: 'DANDEV\ORC_WorkStation_DANDEV_Offline\ExtAttrs\GetThis.csv'
WARNING: Files differ: 'DANDEV\ORC_WorkStation_DANDEV_Offline\Residents\GetThis.csv'
WARNING: Files differ: 'DANDEV\ORC_WorkStation_DANDEV_Offline\SAM\GetThis.csv'
WARNING: Files differ: 'DANDEV\ORC_WorkStation_DANDEV_Offline\Scripts\GetThis.csv'
WARNING: Files differ: 'DANDEV\ORC_WorkStation_DANDEV_Offline\Secure\GetThis.csv'
WARNING: Files differ: 'DANDEV\ORC_WorkStation_DANDEV_Offline\SystemHives\GetThis.csv'
WARNING: Files differ: 'DANDEV\ORC_WorkStation_DANDEV_Offline\TextLogs\GetThis.csv'
WARNING: Files differ: 'DANDEV\ORC_WorkStation_DANDEV_Offline\UserHives\GetThis.csv'
Differing file(s) count: 14
```


### Expand-OrcResults

>**Requirements:** To enable encryption support and .p7b files
>- Openssl (can use included version from https://gitforwindows.org)
>- Compiled unstream in the script's directory - https://github.com/DFIR-ORC/orc-decrypt/blob/master/README.md#cmake-build-instructions-windowslinux

Extract all DFIR-Orc archives referenced by Outcome file(s) recursively.

```
Expand-OrcResults results -Destination out -PrivateKey dfir-orc\configuration\test.key
```


### Expand-OrcArchive
Extract one DFIR-Orc archive recursively (no Outcome needed).

```
Expand-OrcArchive results\w10 -PrivateKey dfir-orc\configuration\test.key -Destination out\w10
```


### Test-OrcExpandedResults
Check DFIR-Orc results using Outcome's commands exit code and do some basic integrity tests.

```
Test-OrcExpandedResults results\w10
```

Output example
```
WARNING: DANDEV: Empty file: Browsers_complet.log
```


### Test-OrcOutcome
Parse a DFIR-Orc Outcome file(s) and print a summary.

```
Test-OrcOutcome .\out\w10\
```

Output example
```
WARNING: WIN-UEP1AAROS8L: Failed ORC_Powershell/Azure_Identity (code: 0x1, duration: 00.00:00:22, start: 11:14:35 AM)
WARNING: WIN-UEP1AAROS8L: Failed ORC_Powershell/DNS_records (code: 0x1, duration: 00.00:00:03, start: 11:14:10 AM)
WARNING: WIN-UEP1AAROS8L: Failed ORC_Powershell/Azure_ScheduledEvents (code: 0x1, duration: 00.00:00:22, start: 11:14:34 AM)
WARNING: WIN-UEP1AAROS8L: Failed ORC_Powershell/Azure_Attested (code: 0x1, duration: 00.00:00:21, start: 11:14:13 AM)
WARNING: WIN-UEP1AAROS8L: Failed ORC_Powershell/Azure_Instance (code: 0x1, duration: 00.00:00:21, start: 11:14:14 AM)
```


### Get-OrcStatistics
Get multiple statistics about DFIR-Orc expanded results.

```
Get-OrcStatistics .\out\w10\
```

Output example
```
WIN-UEP1AAROS8L: Execution: 00.00:17:48, fails: 6, max memory: 1.3 GB (ORC_Little), files: 11 (215.6 MB)
```


### New-OrcLocalConfig
Generate "local configuration" XML output to be used with DFIR-Orc's `/local=<path>` option.

```
New-OrcLocalConfig -PublicKey contoso.com.pem -Upload smb:\\192.168.0.1
```

Output example
```
<dfir-orc>
<recipient name='test' archive='*' >
-----BEGIN CERTIFICATE----- MII...== -----END CERTIFICATE-----
</recipient>
<upload
    job="orc"
    method="filecopy"
    server="smb://192.168.0.1"
    path="/"
    mode="sync"
    operation="copy"
/>
</dfir-orc>
```
