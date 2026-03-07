## [10.2.8] - 2025-11-27
### Added
- Outline: add "always in english" time zone field 'time_zone_key_name'

### Changed
- Yara: update to v4.5.5 (libressl 3.9.2)
- WolfLauncher: Element <input> does not require file to exists before any command execution

### Fixed
- Prevent dll side loading with some configuration when using unsafe directory
- Parameter validation leading to clean exit
- WolfLauncher: option '/help' and a few other were ignored
- WolfLauncher: command is using '/config' overwrite file
