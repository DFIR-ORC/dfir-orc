### Added
- OrcCapsule: minimal binary loader that forward execution to proper DFIR-ORC embedded binary
    - Prepare for the future freeze of DFIR-ORC for Windows XP/2003 to Seven/2008R1
    - Packages DFIR-ORC binaries and selects the most capable one for the host environment: x64, x86, x64-xp, x86-xp, arm64 (experimental)
    - Check ACLs on DFIR-ORC related files (output files, embedded tools)
    - Minimal runtime dependencies
- WolfLauncher: Add '/MultipleInstance' execution option
- WolfLauncher: Allow as default multiple running instances of OrcOffline
- GetThis: add option 'SampleNameFormat' to select other naming scheme for collected sample names
- Outcome: add 'timezoneinfo'

### Changed
- Improve performance by 20% relative to our hardware and configuration
- DFIR-ORC's output files and directories ACLs are not inherited anymore and are now restricted to current user and administrators
- README: update and new instructions for offline builds
- Build toolchain use CMake presets and vcpkg manifest (see README)
- Build-Orc.ps1 script that manages compilation and setup of OrcCapsule
- Replace Azure CI by Github's
- Add ARM64 target (experimental)
- ToolEmbed: New '/embed' option more flexible than '/fromdump' and '/config' with paths
- ToolEmbed: Deprecate 'input', 'run32' and 'run64' option for DFIR-ORC binaries
- ToolEmbed: add option '/force' to allow overwriting output binary
- NTFSInfo: i30info: improve $i30 carving
- Improve CSV generation by being more tolerant following an unexpected error while generating a column
- Removed last parts of legacy PE parser
- Dependencies
    - Removed CLI11
    - Yara v4.5.5 with LibreSSL 4.2.1
    - 7zip 25.1
    - Zstd 1.5.7
    - rapidjson 2025-02-26
    - Fmt 12.1.0
    - ms-gsl 2.8
    - spdlog 1.17.0

### Fixed
- NTFSInfo: i30info: fix parsing sometimes incorrectly carving valid data
- NTFS: multiple fixes on NTFS compressed stream parsing
- WolfLauncher: add missing cli option CommandTimeout
- WolfLauncher: add missing cli option ArchiveTimeout
