### Added
- Upload: 'method="rest"' uploads collected files to an HTTP(S) endpoint through a bounded dialog of '<rest>' steps. See 'tests/RestAgent/README.md'
- WolfLauncher: element '<upload>' can be repeated: the first entry is the primary destination, each later one a fallback tried only when the ones before it failed
    - Each entry applies its own 'operation' and 'include'/'exclude'
    - A failed entry is retried once before falling back to the next
- WolfLauncher: add attribute 'upload="no"' for elements '<outline>', '<outcome>', '<log>' and '<console>'
- NTFS: Wof: decompress '$DATA:WofCompressedData' encoded with LZX, which Windows uses for the Edge install among others
- tests: add 'orca', a tool to extract, query and diff ORC results (see tests/orca/README.md)

### Changed
- WolfLauncher: outline, outcome, log and console are now encrypted when a '<recipient>' is configured, exactly like a command archive
    - Their name gains '.7z.p7b': any tooling reading 'outcome.json' or 'outline.json' after a collection must decrypt it with orc-decrypt
    - Opt out per file with 'encrypt="no"'
    - The log and the console tee are encrypted once closed, so they stay readable on the host while the collection runs
- WolfLauncher: a local configuration ('/local') can turn 'encrypt' and 'upload' off for '<log>', '<console>', '<outline>' and '<outcome>', never on
- WolfLauncher: an archive whose command set was skipped because its output already existed now obeys the '<upload>' 'include'/'exclude' filters
- Remove the ATL dependency
- Build: CMake: add compatibility with ninja, a ninja preset and a preset for clangd
- Build: CMake: honor parent 'CMAKE_MSVC_RUNTIME_LIBRARY' when already set
- Build-Orc.ps1: forward cmake parameters specified after '--'

### Fixed
- WolfLauncher: with 'repeat="Once"', a command set was skipped as soon as the destination held a file with the expected name: the remote size is now compared with the local archive, so a partial upload is sent again (sizes are handled on 64 bits)
- NTFS: read '$Secure:$SDS' entries and the fixed structures of resident attributes only inside their bounds
- NTFS: MFTRecord: account an attribute's length before moving the cursor
- NTFS: MFTWalker: a record whose attributes fail to parse no longer aborts the whole enumeration with an access violation, which left a volume only partly listed
- NTFS: Wof: a compressed file holding its '$REPARSE_POINT' in a child record was collected as a file of zeros, silently; its content is now decompressed
- NTFS: Wof: a compression algorithm with no decompressor no longer voids the whole MFT record: the file keeps being enumerated with its names, dates and sizes, and only its content is refused
- NTFSInfo: i30info: bound the '$I30' FILE_NAME before the callbacks
- RegInfo: prove a registry cell header resident before reading its size
- USNInfo: read a damaged or unreadable offline journal ('$UsnJrnl:$J') best effort, instead of dropping every record following the damage
- PartitionTable: follow the EBR chain iteratively
