# ChangeLog

## [10.2.4] - 2024-02-02
### Added
- Outline: add install_date, install_time and shutdown_time filled from registry
- Outline: add computer_name to match outcome's

### Changed
- Ntfs: improve behavior for compressed buffers when shadow copy block is lost
- GetThis: go to next file in case of errors like unrecoverable deleted file
- Yara: update to v4.4.0
- Yara: replace Windows library with LibreSSL to benefit some features

### Fixed
- ToolEmbed: remove unwanted warning message for run32 and run64 attributes
- Fix breaking stdout redirection on unicode character
- Yara: use yara.exe's workaround for block api (files are now entirely mapped)


## [10.2.3] - 2023-11-15
### Added
- Ntfs: Windows overlay file compression support with resident files
- WolfLauncher: add to element 'command' attribute 'timeout'
- WolfLauncher: Syslog: add some more logs

### Changed
- Yara: do not disable Yara's rule when not referenced in config
- Authenticode: enable ARM PE authenticode check
- Display command line invalid parameter in case of parsing issue

### Fixed
- Ntfs: fix last block decompression
- Ntfs: fix last read position after some decompression
- Ntfs: fix Windows overlay file decompression for some ending block
- Outcome: fix empty process informations for very short lifetime processes
- ci: fix Get-OrcOutcome exit_code existence check


## [10.2.2] - 2023-09-20
### Added
- Allow user to specify any 'key' from 'ORC_Offline' special command set
- NTFSInfo: add columns to volstats.csv for output file name
- WolfLauncher: remove working directory on exit when it was created and empty

### Changed
- Yara: update to 4.3.2

### Fixed
- Yara: possible execution loop issue depending on the rule
- FastFind: in the XML results file the 'Type' values for a registry match was always set to 'Type'
- FastFind: XML output style
- NTFSInfo/FATInfo: unexpected FirstBytes column zero padding


## [10.2.1] - 2023-06-20
### Changed
- Configuration: accept wildcard as exclusion path
- Output filename for shadow copies will be also based on volume identifier
- Limits accept UINT_MAX instead of handling it as "no limits"

### Fixed
- Location is now overridable with cli like others options
- GetThis: fix option 'ResurrectRecord' always set to 'no'
- USNInfo: fix option 'ResurrectRecord' always set to 'yes'
- USNInfo: fix location resolution on some situations with vss
- Fix log file option


## [10.2.0] - 2023-04-20
### Added
- Volume Shadow Copy: add fallback mode when 'vss' service is stopped using directly 'volsnap.sys'
- Volume Shadow Copy: add fully internal parser (no more use of 'volsnap.sys)'
- Volume Shadow Copy: add support for unmounted volumes ("offline" mode)
- Relocate configured DFIR-Orc executable if run from network to avoid issues on disconnections
- NtfsInfo, GetThis, FastFind: add option 'Resurrect=Resident' to process deleted records that are resident in the MFT
- NtfsUtil: enhance '/vss' with more output about shadow copies
- NtfsUtil: add '/vss /dump' mode which will dump copy-on-write tables into json files

### Changed
- GetThis: minimize memory use with WofCompressedData files
- Authenticode: add cache for parsed catalogs
- vcpkg: update dependencies to vcpkg 2023.04.15
- Yara: add cache to improve performance
- WolfLauncher: syslog any upload error

### Fixed
- Fix compatibility with msvc v143 and cpp23
- FastFind: add missing handling of 'Resurrect' and '/ResurrectRecords'


## [10.1.8] - 2023-09-18
### Added
- Allow user to specify any 'key' from 'ORC_Offline' special command set

### Changed
- Yara: update to 4.3.2

### Fixed
- Yara: possible execution loop issue depending on the rule
- FastFind: in the XML results file the 'Type' values for a registry match was always set to 'Type'
- NTFSInfo/FATInfo: unexpected FirstBytes column zero padding
- Fix log file output suboptions


## [10.1.7] - 2023-04-17
### Added
- Add more log in case of memory starvation
- ci: add Compare-OrcOutcome to compare mutiple runs of different versions

### Changed
- Yara: update to version 4.3.0
- Extension library use is now case insensitive (fix XmlLite.dll vs xmllite.dll naming issue)

### Fixed
- Fix compatibility with cmake 3.26.x
- Output path type deduction could be wrong when directory name has '.'
- WolfLauncher: output filename could be split from path with '\0'
- Outline: CpuInfo: compatibility issue with Windows <= W7
- WolfLauncher: x86: virtual memory starvation due to aggressive reservation


## [10.1.6] - 2023-02-20
### Added
- Add check for unknown '/key' option value
- Outline: add job limits, archive timeout values
- GetThis: add support to '/sample' for path matching expressions

### Changed
- Increase supported maximum path length
- WolfLauncher: Add critical log when task are killed

### Fixed
- WolfLauncher: fix missing 'upload' configuration element
- USNInfo: fix shadow copy volume selection
- Outcome: fix sometimes missing command metadata
- Configuration: fix unexpected xml elements handling


## [10.1.5] - 2023-01-17
### Added
- Print configured archive and command timeouts in parameters summary
- Outcome: add output file size in command_set.command.output.size

### Changed
- Improve location exclusion to support path such as 'c:\vss.dd'
- Location exclusion option does not support anymore ',' as separator

### Fixed
- Ntfs: fix parsing for very fragmented volumes
- Authenticode: fix possible incorrect AuthenticodeStatus (thanks Roger)
- Ntfs: add check to avoid infinite loop for corrupted MFT
- Ntfs: fix error handling for nested records possiblity leading to an handled exception
- NtfsInfo: improve performances by reducing IO


## [10.1.4] - 2022-10-24
### Added
- ToolEmbed: add configurations checks for compressed xml settings

### Changed
- Yara: update to version 4.2.3
- Reduce binaries size
- WolfLauncher: update some log level to Critical

### Fixed
- ToolEmbed: fix configurations checks for toolembed import from previous dump
- Volume Shadow Copy: workaround for MS behavior https://github.com/DFIR-ORC/readshadow (expect slower performances with VSS)
- Outcome: fix missing command name when job was interrupted


## [10.1.3] - 2022-09-26
### Added
- Outline, Outcome: add execution id
- Print OS memory statistics on allocation exception
- Test: add helpers

### Changed
- Outline: fix sha1 location to 'dfir-orc.outline.mothership.sha1'
- Display information about memory use on OOM exception
- 7zip: use single thread implementation with no performance hit

### Fixed
- Ntfs: fix rare read issue
- Ntfs: fix read issue on some sparse file
- Bits: upload when on 'single' mode (only 'overwrite' was working)
- Guid: wide string conversion of Guid


## [10.1.2] - 2022-07-20
### Added
- ToolEmbed: add multiple checks to detect bad configurations before deployment

### Changed
- FastFind/GetThis: improve peformance when using multiple Yara rules

### Fixed
- FastFind/GetThis: fix missing error handling (ntfs_find, Yara...)
- Terminate any child processes on WolfLauncher unexpected exit using jobs


## [10.1.1] - 2022-06-20
### Added
- Toolembed: display a message for missing/broken resource because of bad configuration
- Syslog: display configuration on startup

### Changed
- GetThis: replace from output filenames the possible leading '__' with '_'
- Bits: do not create bits job on 'once' mode if server is unreachable
- Log: set default backtrace log level to debug for commands

### Fixed
- Ntfs: fix parsing for very long paths
- Ntfs: fix parsing for very fragmented mfts
- Ntfs: fix possible crash with nested record resolution
- FastFind: fix matching yara rule display when wildcard is used for rule selection
- Configuration: fix missing log option processing for local configuration
- Fix version string for recents windows releases
- Fix possible crash with GetNetworkAdapters
- Fix possible locking issue when uploading console's log
- Bits: add more detail to failure logs
- Bits: fix some missing error handling
- Outcome: fix possible failure when calculating mothership's hash
- Vcpkg: removed uneeded/broken dependency to libwinpthread


## [10.1.0] - 2022-03-25
Summary of changes since the 10.0.24. For more details look at rc versions.
From 10.1.0 the semantic versioning will be applied.

### Added
- Outcome: detailed execution report json output file
- Log: syslog output for very high level logs
- Log: multiple/parellel logs output (execution/investigation/debug)
- Log: add log backtrace option to dump detailed logs (debug...) on error
- Ntfs: add transparent support for WolfCompressedData
- Ntfs: add transparent support for CI.CATALOGHINT
- Ntfs: VolumeShadowCopy: add newest/mid/oldest targetting options
- Add option to exclude volume
- Add option `/nolimit:[<keyword>]` for configurated DFIR-Orc (mothership)
- Test: add tools/ci/test.psm1 to facilitate test automation

### Changed
- Yara: use newest api for better results
- FastFind/GetThis: add statistics for rule profiling
- FastFind/GetThis: add more detailed output on match
- Improve 7z archive compatibility
- Update command line documentation and interface

### Fixed
- Many fixes


## [10.1.0-rc10] - 2022-02-22 - Release Candidate 10
### Added
- test: add functional test helper test.psd1 and test.psm1 (see tools/ci/README.md)
- Authenticode: support for $CI.CatalogHint
- Outcome: add log, console and outline file names
- Outcome: add archive SHA1
- Outcome: add recipients and public keys
- Outcome: add 'input_type' which specify if orc was in 'offline' mode
- Outcome: add to 'Command' expected output files with their origin (file, stderr, ...)
- Outcome: add 'Origin' to 'Command' to specify information about where it comes from
- Outcome: add to 'Command' its SHA1
- Outcome: add to 'Command' the DFIR-Orc tool name if appropriate

### Changed
- Continue on a failed "Location" resolution
- Increase maximum command arguments length
- Only check SecurityDirectory for files with PE header

### Fixed
- Fix p7b encryption support
- Fix network password handling with BITS
- Fix network password handling with CopyFileAgent
- Fix log file upload
- Fix "Location" match expression
- Fix warning due to unhandled legacy columns OWNERID, OWNDERSID
- WolfLauncher: Fix option parsing for '/console'
- ToolEmbed: fix possible race condition with system while updating resource


## [10.1.0-rc9] - 2022-01-07 - Release Candidate 9
### Added
- WolfLauncher: expand environment variables for `<Argument>` xml configuration element.
- NTFSInfo: add 'DataSize' column to 'I30Info' output for $I30's file size
- NTFSInfo: add 'MountPoint' column to 'volstats.csv' with assigned drive letter
- FATInfo: add 'MountPoint' column to 'volstats.csv' with assigned drive letter
- Add security descriptor binary dump

### Changed
- vcpkg: update dependencies to 2021.12.01
- Continue execution if one of the specified location is not resolved
- GetThis: move Statistics.json into GetThis archive

### Fixed
- 7z archives: empty file handling for compatibility
- WolfLauncher: missing stdout log file upload for a configurated Orc
- WolfLauncher: fix possible path issue when using '/out'
- GetThis: fix missing csv when using directory as output
- GetThis: fix possible missing sample when having multiple matches
- Fix configuration when using '{UserProfiles}' with a path appended


## [10.1.0-rc8] - 2021-12-02 - Release Candidate 8
### Added
- Location: add options for shadow copy volumes specification (ex: '/shadows=<newest|mid|oldest|{GUID}>')
- Location: add options for location exclusion (ex: '/exclude="%SYSTEMDRIVE%,D:')
- FastFind, GetThis: display statistics for 'ntfs_find' rules
- FastFind: write attribute name (ADS) on matching elements
- FastFind: write mathing yara rules when selected with '*'
- Flush logs on unexpected exit (Ctrl + C, ...)
- tools: ci: vs2022 toolchain support

### Changed
- Yara: use version 4.1.3
- FastFind, GetThis: yara rule optimisation with new Yara API
- Allow extracted resource execution
- Improve performances when lot of lines are printed
- Log: flush on error log level
- Parquet: improve utf-8 support

### Fixed
- 7z: fix empty file handling for compatibility
- Fix pre-existing archive upload on new executions
- Fix missing output directory from archive for third parties
- fastfind, getthis: fix registry match false positive
- Ntfs: fix WofCompressedData decompression
- Ntfs: fix file access issue when requesting a closed stream
- Syslog: fix option 'port' parsing
- WolfLauncher: fix console file output path


## [10.1.0-rc7] - 2021-10-15 - Release Candidate 7
### Added
- Add support for $DATA:WofCompressedData
- WolfLauncher: add option `/nolimits[:<keyword>,...]`
- WolfLauncher: embed configuration specified with '/config=<path>'
- Outcome: add 'start' element to specify start of execution
- Outcome: add 'end' element to specify end of execution
- Upload target now support environment variables

### Changed
- Add logs on uploaded file status check
- Outline: add 'outline' root node
- Outline: rename "time" element to "start" and make it ISO
- Outcome: use sha1 as default algorithm for Orc self hash
- vcpkg: update to 2021.05.12 (fmt 8.0.1, spdlog: 1.9.2, ...)
- FastFind: use full computer name as computer name
- Log: improve backtrace for more complex configurations
- Remove boost-serialization unused dependency
- Log: Use same timestamp for Journal's log and syslog messages

### Fixed
- GetSamples: Fix authenticode handling
- Fix XPSP2 XmlLite.dll loading
- Outline: fix element 'timestamp'
- GetThis: handle zip output (using 7z library)
- FastFind: fix match description for 'data_contains_hex'
- FastFind: fix format string issue for keys with braces
- Fix missing API with XPSP2
- Fix unexpected log message on success
- Fix performance issue with NTFS compressed files
- GetSamples: fix authenticode signature check with '/sampleinfo'

### Removed
- cmake: Remove option ORC_BUILD_TLSH


## [10.1.0-rc6] - 2021-06-22 - Release Candidate 6
### Changed
- GetSamples: enable autorunsc for all users
- yara: update to 4.0.2
- NtfsUtil: enhanced property display

### Fixed
- fastfind: fix xml configuration parsing ("invalid index")
- fastfind: fix failure when no '/out' was specified


## [10.1.0-rc5] - 2021-03-12
### Changed
- Log: apply backtrace trigger level command line option earlier

### Fixed
- Build: add support for Visual Studio 16.9
- GetThis: fix output archive creation
- Multiple minor fixes: see commits for more details
- NtfsUtil: fix missing dump of properties
- Log: fix console's backtrace option handling

### Added
- Log: add syslog forwarding to WolfLauncher's timestamped output
- Log: add new log options usage
- Log: add start/end timestamp of WolfLauncher to journal
- Log: add DFIR-Orc version of WolfLauncher's binary to journal

### Removed
- yara: remove libressl dependency and rely on wincrypt api
- yara: remove jansson dependency and cuckoo module


## [10.1.0-rc4] - 2021-02-12
### Changed
- Do not exit after manageable formatting exception
- cleanup: src/OrcCommand source tree layout and remove dead code
- cmake: better handling of `WINVER`, `_WIN32_WINNT`, `NTDDI_VERSION` along with vcpkg

### Fixed
- Configuration: handle correctly unknown XML elements
- Embeded resources lookup for in-memory use
- GetSamples: handle correctly 'log' element configuration (not 'logging')
- Log: most of the commands share now the same logging capabilities
- WolfLauncher: UTC timestamp in console (or journal)
- Fix SSDeep use

### Added
- Outcome: write execution summary into the file specified in WolfLauncher configuration with 'outcome' element
- Enable SWAPRUN:NET flag to ensure stability when executed from network share
- WolfLauncher: 'console' configuration element to tee output to a file
- Log: new 'log' configuration element replacing 'logging' becoming deprecated
- Log: configure separate log and backtrace levels for File and Console
- Log: add advanced command line support for all log/console options
- Log: the specified log file path support patterns (like '{TimeStamp}')
- cmake: ORC_BUILD_TLSH option (default: OFF)

### Removed
- 'JobStatistics' (replaced by 'outcome')
- 'ProcessStatistics' (replaced by 'outcome')


## [10.1.0-rc3] - 2020-12-02
### Changed
- Log: enhance FileSink synchronisation

### Fixed
- WolfLauncher: fix log file format for a configurated DFIR-Orc
- Log: add support for SPDLOG_LEVEL env variable
- Values incorrectly displayed as addresses instead of readable strings
- Outline: missing 'qfe', 'network', 'environment' sections
- GetSectors: failure when no disk was specified
- Column filter processing inserting some empty lines (',,,,,,,,,,,,,,,')
- GetThis: fix compression level

### Added
- WolfLauncher: print a 'Ended' line in the journal with statistics

### Removed
- Dead code: ByteStreamSink


## [10.1.0-rc2] - 2020-11-20
### Changed
- Column 'ParentName': remove trailing '\'
- Autorunsc: use UTC time format
- vcpkg: update spdlog to v1.8.1

### Fixed
- Parsing issue on some MFT
- Output: fix 7z creation causing crashes
- Log: format system errors as unsigned hex values
- Log: option 'StdOutErr' will now properly includes 'stderr' output
- Log: move many errors from 'Error' level to 'Critical'
- Log: fix shadowed formatters causing some unformatted output value
- Console: fix unformatted output value of 'filters' parameter
- Option: partially ignored options '/Computer', '/FullComputer', '/SystemType'
- OrcApache: calling convention mismatch on x86 causing crashes

### Added
- Usage: add missing usage options
- WolfLauncher: print real file size for compressed items
- WolfLauncher: print archive size once completed
- WolfLauncher: print file size for queued upload items

### Removed
- Dead code: 'OutputFileOption', 'OutputDirOption'


## [10.1.0-rc1] - 2020-11-09
### Changed
- GetThis: reduce memory usage by a factor of 3 depending on configuration
- GetThis: major refactor to allow some future optmizations
- Log: split stdout and stderr data
- Log: use spdlog log library
- Log: dump log backtrace on critical errors
- Log: use new log levels
- Log: add utc timestamps (ISO 8601)
- Improve DFIR-Orc temporary directory removal
- OrcCommand: major refactor
- README: add license section
- vcpkg: update to 2020.11-1

### Fixed
- GetThis: segfault when hash xml element is empty #69
- GetThis: do not make hash if they are not configured #67
- GetThis: segfault with MaxSampleCount=0 and reportall/hash options #8
- Log: catch early logs in log files
- vcpkg: fix shared build
- ... Check commit for a complete list of other fixes

### Added
- Outline: Add '/Outline' cli option
- Build: Add compatibility with clang and llvm tools
- Build: Add compatibility with ninja
- Build: Add compatibility with VS 2019 16.8
- Build: Add option ORC_BUILD_BOOST_STACKTRACE
- Build: Enhance tools/ci/build.ps1 script
- Add support archive hierarchies in resources
- CI: Azure: Add support for Azure Artifacts

### Removed
- Remove ImportData tool
- Remove ChakraCore component
- Remove OrcSql component
- Remove dead code


## [10.0.24] - 2022-02-22
### Changed
- Continue on a failed "Location" resolution

### Fixed
- Fix "Location" match expression
- Fix warning due to unhandled legacy columns OWNERID, OWNDERSID
- Fix network password handling with BITS
- Fix network password handling with CopyFileAgent


## [10.0.23] - 2021-12-16
### Fixed
- 7z archives: empty file handling for compatibility

### Added
- WolfLauncher: expand environment variables for `<Argument>` xml configuration element.
- NTFSInfo: add 'DataSize' column to 'I30Info' output for $I30's file size
- NTFSInfo: add 'MountPoint' column to 'volstats.csv' with assigned drive letter
- FATInfo: add 'MountPoint' column to 'volstats.csv' with assigned drive letter


## [10.0.22] - 2021-12-01
### Changed
- Yara: Update to 4.1.3
- Allow extracted resources to be executed

### Fixed
- Fix missing upload for pre-existing archive
- FastFind, GetThis: fix registry match false positive
- Log: Fix log option parsing for compatibility with 10.1.x usage
- Log: Fix log flushing on unexpected exit
- 7z archives: empty file handling for compatibility

### Added
- FastFind, GetThis: print ADS name on match
- FastFind, GetThis: print yara rule name on match


## [10.0.21] - 2021-10-07
### Changed
- FastFind: use full computer name as 'computer' output element

### Fixed
- Add missing PE machine type
- Yara: Fix log formatting
- FastFind: Fix match description for 'data_contains_hex'


## [10.0.20] - 2021-07-02
### Added
- WolfLauncher: add option `/nolimits[:<keyword>,...]`


## [10.0.19] - 2021-06-01
### Changed
- yara: update to 4.0.2
- GetSamples: enable autorunsc for all users

### Fixed
- fastfind: fix nullptr dereference when '/out' is not specified


## [10.0.18] - 2021-03-10
### Changed
- yara: remove libressl dependency and rely on wincrypt api
- yara: remove jansson dependency and cuckoo module
- fmt: update to 7.0.3
- spdlog: update to 1.8.1


## [10.0.17] - 2021-02-10
### Changed
- build.ps1: stop on any error
- build.ps1: multiple other improvements

### Fixed
- Embeded resources lookup for in-memory use
- Configuration: do not exit on unknown configuration element

### Added
- build.ps1: option to build ssdeed
- build.ps1: option to specify vcpkg directory
- Configuration: compatibility with upcoming 10.1.x new log options


## [10.0.16] - 2020-11-09
### Added
- CI: Azure: add support for release/* branches
- SystemDetails: add 'Windows' and 'RTM' tag for pre-Win10 versions
- NtfsInfo: add SecurityDirectorySize and SecurityDirectorySignatureSize

### Fixed
- CsvFileWriter: fix memory corruption


## [10.0.15] - 2020-09-28
### Added
- New 'Location' configuration keyword: '{UserProfiles}' (get profiles directories from HKLM/SOFTWARE/Microsoft/Windows NT/CurrentVersion/ProfileList)

### Fixed
- BITS: Archive were skipped when BITS server was unavailable
- CSV: Two possible csv corruptions
