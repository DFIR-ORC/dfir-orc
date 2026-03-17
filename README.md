# DFIR ORC
[![LGPL licensed][img-license]](./LICENSE.txt)
[![badge_repo](https://img.shields.io/badge/DFIR--ORC-DFIR--ORC-white)](https://github.com/DFIR-ORC/dfir-orc)
[![category_badge_external](https://img.shields.io/badge/category-external-%23b556b6)](https://anssi-fr.github.io/README.en.html#project-categories)
[![openess_badge_A](https://img.shields.io/badge/code.gouv.fr-collaborative-blue)](https://documentation.ouvert.numerique.gouv.fr/les-parcours-de-documentation/ouvrir-un-projet-num%C3%A9rique/#niveau-ouverture)

<img src="res/dfir-orc-logo.png" alt="DFIR-ORC" height="128"><img src="res/anssi-logo.png" alt="ANSSI logo" height="128">

## French Cybersecurity Agency (ANSSI)

*This project is managed by [ANSSI](https://cyber.gouv.fr/). To find out more, you can visit the [page](https://cyber.gouv.fr/enjeux-technologiques/open-source/) (in French) dedicated to ANSSI’s open-source strategy. You can also click on the badges above to learn more about their meaning.*

## Documentation
https://dfir-orc.github.io

## Build Status

| Branch       | Status |
|:-------------|:-------|
| main         | [![Build Status](https://github.com/DFIR-ORC/dfir-orc/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/DFIR-ORC/dfir-orc/actions/workflows/build.yml) |
| release/10.3 | [![Build Status](https://github.com/DFIR-ORC/dfir-orc/actions/workflows/build.yml/badge.svg?branch=release%2F10.3.x)](https://github.com/DFIR-ORC/dfir-orc/actions/workflows/build.yml) |

---

## Quick Start

```powershell
winget install Microsoft.Git

# Copy .vsconfig file or clone the repository and install Visual Studio
git clone --recursive https://github.com/dfir-orc/dfir-orc.git
cd dfir-orc
winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--passive --config .vsconfig"

Import-Module "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" -SkipAutomaticLocation

.\Build-Orc.ps1  # Powershell >=5.1
```

The script builds the x86-xp, x64-xp versions of DFIR-ORC and then packages them into `DFIR-ORC.exe`.

To also embed a configuration at build time using ToolEmbed:

```powershell
.\Build-Orc.ps1 -ToolEmbed .\config
```

To build specific platforms or multiple configurations:

```powershell
.\Build-Orc.ps1 -BuildConfig Debug,MinSizeRel -Platform x64-xp,x86-xp,x64,x86
```

---

## Build Reference

### Requirements
- **Visual Studio** 2022 to 2026, **English language pack only** (vcpkg limitation)
  - Select workload: *Desktop development with C++*
  - Import the provided [.vsconfig](.vsconfig) in Visual Studio Installer
  - To build test add:
      - "Microsoft.VisualStudio.Component.VC.TestAdapterForBoostTest"
      - "Microsoft.VisualStudio.Component.VC.TestAdapterForGoogleTest",
      - "Microsoft.VisualStudio.Component.VC.UnitTest"
- **PowerShell** 5.1+ (for `Build-Orc.ps1`)

A clean Windows 11 build environment can be provisioned using **Hyper-V Quick Create** (search for *Hyper-V Quick Create* in the Start menu). This creates a local VM from an evaluation image without requiring a separate download. Remaining dependencies can be installed inside the VM using 'Visual Studio Installer' by selecting 'Desktop development with C++'.

---

### Using Build-Orc.ps1 (`Build-Orc.ps1`)

To build without the script, see [Manual Build](#manual-build).

The script drives the full pipeline: configure > build > package with OrcCapsule > optionally embed a configuration with ToolEmbed.

```
.\Build-Orc.ps1 [[-Source] <path>]
                [[-BuildConfig] <Debug|MinSizeRel|RelWithDebInfo>[,...]]
                [[-Platform] <platform>[,...]]
                [[-BuildDir] <path>]
                [[-ToolEmbed] <config-dir>]
                [-ConfigureOnly]
                [-BuildOnly]
                [-FastFind]
```

| Parameter        | Default              | Description |
|:-----------------|:---------------------|:------------|
| `-Source`        | *(current dir)*      | Root of the DFIR-ORC source tree |
| `-BuildConfig`   | `MinSizeRel`         | One or more CMake build configurations: `Debug`, `MinSizeRel`, `RelWithDebInfo` |
| `-Platform`      | `x64-xp`, `x86-xp`  | One or more target platforms; each must match a `dfir-orc-<platform>` CMake preset |
| `-BuildDir`      | `.\build`            | Root directory for all CMake artifacts and packaged output |
| `-ToolEmbed`     | *(none)*             | If set, runs a ToolEmbed step using this directory as configuration source |
| `-ConfigureOnly` | *(off)*              | Run cmake configure for all presets then stop; skip build and packaging |
| `-BuildOnly`     | *(off)*              | Run configure and build, but skip the packaging step |
| `-FastFind`      | *(off)*              | Also build and package `FastFind.exe` in addition to `DFIR-ORC.exe` |

Packaged executables are written to `<BuildDir>\<config>\DFIR-ORC.exe` (and `FastFind.exe` when `-FastFind` is set). When `-ToolEmbed` is used the configuration-embedded output is written to `<BuildDir>\<config>\DFIR-ORC-ready.exe`.

**Examples:**

```powershell
# Default release build
.\Build-Orc.ps1

# Debug build in a custom build directory
.\Build-Orc.ps1 -BuildConfig Debug -BuildDir .\out\debug

# Build two configurations in one pass for all default platforms
.\Build-Orc.ps1 -BuildConfig Debug,MinSizeRel

# Build for additional platforms (including post Seven/2008-R1 presets)
.\Build-Orc.ps1 -Platform x64-xp,x86-xp,x64,x86

# Release build with embedded configuration
.\Build-Orc.ps1 -BuildConfig RelWithDebInfo -ToolEmbed .\config

# Also produce a FastFind package alongside DFIR-ORC
.\Build-Orc.ps1 -FastFind

# Configure only (useful to pre-download vcpkg dependencies)
.\Build-Orc.ps1 -ConfigureOnly
```

#### What the script does

1. Configures the `capsule-x86-xp` preset plus one `dfir-orc-<platform>` preset per value in `-Platform`, placing each binary tree under `<BuildDir>\<preset-name>`.
2. Builds every configured preset for each value in `-BuildConfig`.
3. For each build configuration, runs **OrcCapsule** to bundle the per-platform `DFIR-ORC_<arch>.exe` artifacts into a single self-selecting `DFIR-ORC.exe`. When `-FastFind` is set the same bundling is done for `FastFind.exe`.
4. *(Optional)* Runs **ToolEmbed** (via the newly packaged capsule) to inject the XML configuration and tools from `-ToolEmbed`, producing `DFIR-ORC-ready.exe`.

---

### Manual Build

Use a *Developer Command Prompt for VS 2022* (avoid plain `cmd.exe`).

#### Build
```powershell
git clone --recursive https://github.com/dfir-orc/dfir-orc.git
cd dfir-orc

# Configure
cmake --preset capsule-x86-xp
cmake --preset dfir-orc-x64-xp
cmake --preset dfir-orc-x86-xp

# Build (replace MinSizeRel with your target configuration)
cmake --build --preset capsule-x86-xp-MinSizeRel
cmake --build --preset dfir-orc-x64-xp-MinSizeRel
cmake --build --preset dfir-orc-x86-xp-MinSizeRel

# Encapsulation
.\build\capsule-x86-xp\MinSizeRel\OrcCapsule.exe capsule add `
    .\build\dfir-orc-x86-xp\MinSizeRel\DFIR-ORC_x86.exe `
    .\build\dfir-orc-x64-xp\MinSizeRel\DFIR-ORC_x64.exe `
    --output .\build\DFIR-ORC.exe `
    --force
```

#### Configuration
```powershell
$CONFIG_DIR = "C:\path\to\your\orc-config"
.\build\DFIR-ORC.exe ToolEmbed /embed="$CONFIG_DIR\config" /out=DFIR-ORC.exe
```

#### [DEPRECATED] Configuration using old build.cmd
```powershell
$CONFIG_DIR = "C:\path\to\your\orc-config"
cp .\build\DFIR-ORC.exe "$CONFIG_DIR\tools\DFIR-ORC_x64.exe"
cd $CONFIG_DIR
.\build.cmd
```

Both 32-bit and 64-bit versions should be built for maximum compatibility before deployment. See https://dfir-orc.github.io for deployment and configuration details.

### Offline Build

#### On the online machine
Setup the build environment by following the [Quick Start](#quick-start) section but stop before running `.\Build-Orc.ps1`.

```powershell
$ORC_MIRROR = "c:\users\foo\mirror"
$ENV:VCPKG_DOWNLOADS="$ORC_MIRROR\vcpkg-downloads"

# Populate the vcpkg download directory
New-Item -type directory $ORC_MIRROR
git clone --recursive https://github.com/dfir-orc/dfir-orc.git
cd dfir-orc
# Don't use 'vcpkg install --download-only ...' which is as they state "best-effort"
.\Build-Orc.ps1 -ConfigureOnly -Platform x86-xp,x64-xp,x86,x64

Copy-Item "external/vcpkg/vcpkg.exe" $ORC_MIRROR/

# Mirror the repositories
git clone --mirror https://github.com/dfir-orc/dfir-orc.git "$ORC_MIRROR/dfir-orc.git"
git clone --mirror https://github.com/microsoft/vcpkg.git "$ORC_MIRROR/vcpkg.git"
```

Transfer the `$ORC_MIRROR` directory to the offline machine.

#### On the offline machine
Ensure Visual Studio 2022 with the *Desktop development with C++* workload and
PowerShell 5.1+ are installed before proceeding.

```powershell
# Adjust to where the mirror was transferred
$ORC_MIRROR = "C:\Users\bar\mirror"
$ENV:VCPKG_DOWNLOADS="$ORC_MIRROR\vcpkg-downloads"  # 'Tools' subdirectory is required
```

**Option A — internal git server**: push the mirrors first, then clone from the server:
```powershell
git push --mirror https://internal.git/mirror/dfir-orc.git
git push --mirror https://internal.git/mirror/vcpkg.git

git clone https://internal.git/mirror/dfir-orc.git
cd dfir-orc
git -c url."https://internal.git/mirror/vcpkg.git".insteadOf="https://github.com/microsoft/vcpkg.git" `
    submodule update --init
```

**Option B — local filesystem**:
```powershell
git clone "$ORC_MIRROR/dfir-orc.git"
cd dfir-orc
git -c protocol.file.allow=always `
    -c url."file:///$ORC_MIRROR/vcpkg.git".insteadOf="https://github.com/microsoft/vcpkg.git" `
    submodule update --init
```

**Then build**:
```powershell
Copy-Item $ORC_MIRROR/vcpkg.exe "external/vcpkg/"
.\Build-Orc.ps1
```

---

## License

The contents of this repository are available under the [LGPL 2.1+ license](LICENSE.txt).  
The name DFIR ORC, the associated logo and the ANSSI logo belong to ANSSI, no use is permitted without express approval.

---

Le contenu de ce dépôt est disponible sous licence LGPL 2.1+, tel qu'indiqué [ici](LICENSE.txt).  
Le nom DFIR ORC, le logo associé et le logo de l'ANSSI appartiennent à l'ANSSI, aucun usage n'est permis sans autorisation expresse.

---

## Acknowledgments

DFIR ORC is disclosing Microsoft source code with Microsoft's permission.

[img-license]: https://img.shields.io/github/license/DFIR-ORC/dfir-orc
