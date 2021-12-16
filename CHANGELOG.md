# ChangeLog

## [10.0.23] - 2021-12-16
### Fixed
- 7z archives: empty file handling for compatibility

### Added
- WolfLauncher: expand environment variables for `<Argument>` xml configuration element.
- NTFSInfo: add 'DataSize' column to 'I30Info' output for $I30's file size
- NTFSInfo: add 'MountPoint' column to 'volstats.csv' with assigned drive letter
- FATInfo: add 'MountPoint' column to 'volstats.csv' with assigned drive letter


## [10.0.22] - 2021-12-01
### changed
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
