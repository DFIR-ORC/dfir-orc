[![Build Status](https://dev.azure.com/jeangautier/dfir-orc/_apis/build/status/DFIR-ORC.dfir-orc?branchName=release/10.0.x)](https://dev.azure.com/jeangautier/dfir-orc/_build/latest?definitionId=1&branchName=release/10.0.x) [![LGPL licensed][img-license]](./LICENSE.txt)


# DFIR ORC

## Documentation
https://dfir-orc.github.io


## Build

### Requirements
- Visual Studio >=2017 with this [configuration](.vsconfig) or alternatively use [vstools](docs/vstools/vstools.md)
- Kitware's CMake >= 3.12 or Visual Studio 2017 integrated version
- LLVM's Clang Format >= 8.0.0 or Visual Studio 2019 integrated version

NB: Visual Studio 2019 16.3 (and 16.4 preview 1) can now compile DFIR ORC.

Build environment can be setup quickly using [Microsoft's developer virtual machines](https://developer.microsoft.com/en-us/windows/downloads/virtual-machines). Import this [.vsconfig](.vsconfig) from **Visual Studio Installer**.


### Commands
Both 32-bit and 64-bit versions should be built for maximum compatiliby before deployment. See https://dfir-orc.github.io for more details about deployment and configuration.

In a prompt like *Developer Command Prompt for VS 2019* (prefer to avoid using *cmd.exe*):

```bash
git clone --recursive https://github.com/dfir-orc/dfir-orc.git
cd dfir-orc
mkdir build-x86 build-x64

cd build-x86
cmake -G "Visual Studio 16 2019" -A Win32 -T v141_xp ..
cmake --build . --config MinSizeRel -- -maxcpucount

cd ../build-x64
cmake -G "Visual Studio 16 2019" -A x64 -T v141_xp ..
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
| ORC_BUILD_TLSH       | OFF                   | Build with tslh support          |
| ORC_BUILD_JSON       | ON                    | Build with JSON enabled          |
| ORC_USE_STATIC_CRT   | ON                    | Use static runtime               |
| ORC_VCPKG_ROOT       | ${ORC}/external/vcpkg | VCPKG root directory             |
| ORC_XMLLITE_PATH     |                       | XmlLite.dll path (xp sp2)        |
| VCPKG_TARGET_TRIPLET | Autodetect            | VCPKG triplet to use             |
| CMAKE_TOOLCHAIN_FILE | Autodetect            | VCPKG's toolchain file           |

[1] The `xmllite.dll` is native after patched Windows XP SP2

**Note:** Some combinations may be irrelevant.


### Build vcpkg dependencies manually
See top **CMakeLists.txt** for a complete list of the dependencies to install. Building mainstream vcpkg may not work as some packages have custom patches. The **VERSION.txt** contains the reference commit from official vcpkg repository.

```bash
cd external/vcpkg
bootstrap-vcpkg.bat
vcpkg --vcpkg-root . install fmt:x64-windows-static ...
```

## License

The contents of this repository is available under [LGPL2.1+ license](LICENSE.txt).
The name DFIR ORC and the associated logo belongs to ANSSI, no use is permitted without express approval.

---

Le contenu de ce dépôt est disponible sous licence LGPL2.1+, tel qu'indiqué [ici](LICENSE.txt).
Le nom DFIR ORC et le logo associé appartiennent à l'ANSSI, aucun usage n'est permis sans autorisation expresse.


## Acknowledgments
DFIR ORC is disclosing Microsoft source code with Microsoft's permission.

[img-build]: https://dev.azure.com/jeangautier/dfir-orc/_apis/build/status/jeangautier.dfir-orc?branchName=master
[img-license]: https://img.shields.io/github/license/DFIR-ORC/dfir-orc
