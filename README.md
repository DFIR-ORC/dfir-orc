# DFIR ORC
[![LGPL licensed][img-license]](./LICENSE.txt)
[![badge_repo](https://img.shields.io/badge/DFIR--ORC-DFIR--ORC-white)](https://github.com/DFIR-ORC/dfir-orc)
[![category_badge_external](https://img.shields.io/badge/category-external-%23b556b6)](https://anssi-fr.github.io/README.en.html#project-categories)
[![openess_badge_A](https://img.shields.io/badge/code.gouv.fr-collaborative-blue)](https://documentation.ouvert.numerique.gouv.fr/les-parcours-de-documentation/ouvrir-un-projet-num%C3%A9rique/#niveau-ouverture)

<img src="https://dfir-orc.github.io/_static/logo.jpg" alt="DFIR-ORC" height="128"><img src="https://www.sgdsn.gouv.fr/files/styles/ds_image_paragraphe/public/files/Notre_Organisation/logo_anssi.png" alt="ANSSI logo" height="128">

## French Cybersecurity Agency (ANSSI)

*This projet is managed by [ANSSI](https://cyber.gouv.fr/). To find out more, you can visit the [page](https://cyber.gouv.fr/enjeux-technologiques/open-source/) (in French) dedicated to ANSSI’s open-source strategy. You can also click on the badges above to learn more about their meaning.*
## Documentation
https://dfir-orc.github.io


## Build
| Branch       | Status                                                                                                                                                                                                                          |
|:-------------|:--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| main         | [![Build Status](https://dev.azure.com/dfir-orc/dfir-orc/_apis/build/status/DFIR-ORC.dfir-orc?branchName=main)](https://dev.azure.com/dfir-orc/dfir-orc/_build/latest?definitionId=1&branchName=main)                     |
| release/10.1 | [![Build Status](https://dev.azure.com/dfir-orc/dfir-orc/_apis/build/status/DFIR-ORC.dfir-orc?branchName=release/10.1.x)](https://dev.azure.com/dfir-orc/dfir-orc/_build/latest?definitionId=1&branchName=release/10.1.x) |
| release/10.2 | [![Build Status](https://dev.azure.com/dfir-orc/dfir-orc/_apis/build/status/DFIR-ORC.dfir-orc?branchName=release/10.2.x)](https://dev.azure.com/dfir-orc/dfir-orc/_build/latest?definitionId=1&branchName=release/10.2.x) |

### Requirements
- Visual Studio
  - From 2017 to 2022
  - English only (vcpkg limitation)
  - Use this installer [configuration](.vsconfig) or alternatively use [vstools](docs/vstools/vstools.md)
  - Check also "Desktop development with C++"
- Kitware's CMake >= 3.25 or Visual Studio integrated version

Build environment can be setup quickly using [Microsoft's developer virtual machines](https://developer.microsoft.com/en-us/windows/downloads/virtual-machines). Import this [.vsconfig](.vsconfig) from **Visual Studio Installer**.


### Commands
Both 32-bit and 64-bit versions should be built for maximum compatiliby before deployment. See https://dfir-orc.github.io for more details about deployment and configuration.

In a prompt like *Developer Command Prompt for VS 2019* (prefer to avoid using *cmd.exe*):

```bash
git clone --recursive https://github.com/dfir-orc/dfir-orc.git
cd dfir-orc
mkdir build-x86 build-x64

cd build-x86
cmake -G "Visual Studio 17 2022" -A Win32 -T v141_xp ..
cmake --build . --config MinSizeRel -- -maxcpucount

cd ../build-x64
cmake -G "Visual Studio 17 2022" -A x64 -T v141_xp ..
cmake --build . --config MinSizeRel -- -maxcpucount
```

* The `-T v141_xp` option will allow compatibility with Windows XP SP2 and later, it can safely be removed if this is not required.

* The default `ORC_BUILD_VCPKG=ON` option will build vcpkg packages in 'external/vcpkg' subdirectory.

**Important** Always do a `git submodule update` after any `git pull` to update submodules aswell. Alternatively, always pull with `git pull --recurse-submodules`


### Options

| CMake option         | Default               | Description                      |
|:---------------------|:----------------------|:---------------------------------|
| ORC_DOWNLOADS_ONLY   | OFF                   | Only download vcpkg dependencies |
| ORC_BUILD_VCPKG      | ON                    | Build vcpkg dependencies         |
| ORC_BUILD_APACHE_ORC | OFF                   | Build Apache Orc module          |
| ORC_BUILD_COMMAND    | ON                    | Build OrcCommand library         |
| ORC_BUILD_FASTFIND   | OFF                   | Build FastFind binary            |
| ORC_BUILD_ORC        | ON                    | Build Orc binary                 |
| ORC_BUILD_PARQUET    | OFF                   | Build Parquet module (x64)       |
| ORC_BUILD_SSDEEP     | OFF                   | Build with ssdeep support        |
| ORC_BUILD_JSON       | ON                    | Build with JSON enabled          |
| ORC_USE_STATIC_CRT   | ON                    | Use static runtime               |
| ORC_VCPKG_ROOT       | ${ORC}/external/vcpkg | VCPKG root directory             |
| ORC_XMLLITE_PATH     |                       | XmlLite.dll path (xp sp2)        |
| VCPKG_TARGET_TRIPLET | Autodetect            | VCPKG triplet to use             |
| CMAKE_TOOLCHAIN_FILE | Autodetect            | VCPKG's toolchain file           |

[1] The `xmllite.dll` is native after patched Windows XP SP2

**Note:** Some combinations may be irrelevant.


## License

The contents of this repository is available under [LGPL2.1+ license](LICENSE.txt).
The name DFIR ORC, the associated logo and the ANSSI logo belong to ANSSI, no use is permitted without express approval.

---

Le contenu de ce dépôt est disponible sous licence LGPL2.1+, tel qu'indiqué [ici](LICENSE.txt).
Le nom DFIR ORC, le logo associé et le logo de l'ANSSI appartiennent à l'ANSSI, aucun usage n'est permis sans autorisation expresse.


## Acknowledgments
DFIR ORC is disclosing Microsoft source code with Microsoft's permission.

[img-license]: https://img.shields.io/github/license/DFIR-ORC/dfir-orc
