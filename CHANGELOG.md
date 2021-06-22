# ChangeLog

## [10.1.0-rc6] - 2021-06-22 - Release Candidate 5
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


## [10.0.19] - 2021-01-06
### Changed
- yara: update to 4.0.2
- GetSamples: enable autorunsc for all users

### Fixed
- fastfind: fix nullptr dereference when '/out' is not specified


## [10.0.18] - 2021-10-03
### Changed
- yara: remove libressl dependency and rely on wincrypt api
- yara: remove jansson dependency and cuckoo module
- fmt: update to 7.0.3
- spdlog: update to 1.8.1


## [10.0.17] - 2021-10-02
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
