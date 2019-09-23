include(vcpkg_common_functions)

set(TLSH_VERSION 3.13.1)
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO trendmicro/tlsh
    REF 3.13.1
    SHA512 45cf35dc9e5b12758419bd8c2e49fa0f2160ab9b77a204df021df8058b4ca4220fd0b6db7f3cadf245bfbcf7481865177c4e893b06e85da6801133947808c1b4
    HEAD_REF master
)

vcpkg_apply_patches(
    SOURCE_PATH ${SOURCE_PATH}
    PATCHES
        "${CMAKE_CURRENT_LIST_DIR}/fix-version-header.patch"
        "${CMAKE_CURRENT_LIST_DIR}/fix-tlsh-header.patch"
)

file(COPY ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt DESTINATION ${SOURCE_PATH})
file(COPY ${CMAKE_CURRENT_LIST_DIR}/src/CMakeLists.txt DESTINATION ${SOURCE_PATH}/src)
file(COPY ${CMAKE_CURRENT_LIST_DIR}/Windows/CMakeLists.txt DESTINATION ${SOURCE_PATH}/Windows)
file(COPY ${CMAKE_CURRENT_LIST_DIR}/VERSION.in DESTINATION ${SOURCE_PATH}/cmake)
file(COPY ${CMAKE_CURRENT_LIST_DIR}/version.h.in DESTINATION ${SOURCE_PATH}/cmake)

vcpkg_configure_cmake(
    SOURCE_PATH ${SOURCE_PATH}
    PREFER_NINJA
    OPTIONS_DEBUG -DTLSH_NO_INSTALL_HEADERS=ON
)

vcpkg_install_cmake()

vcpkg_fixup_cmake_targets(CONFIG_PATH share/tlsh)

vcpkg_copy_pdbs()

file(
    INSTALL ${CMAKE_CURRENT_LIST_DIR}/License.txt
    DESTINATION ${CURRENT_PACKAGES_DIR}/share/tlsh
    RENAME copyright
)

vcpkg_test_cmake(PACKAGE_NAME tlsh)
