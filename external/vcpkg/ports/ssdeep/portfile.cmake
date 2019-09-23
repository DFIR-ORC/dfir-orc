include(vcpkg_common_functions)

set(SSDEEP_VERSION 2.14.1)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ssdeep-project/ssdeep
    REF release-2.14.1
    SHA512 6f45a961800fcbd4a5c8f1dac9afc7e0791ecd5aded28f3048d4ade68870a8e928704c80a5778a463b9492d561ae4568785c2b8c873f485d5d9c500d74955f07
    HEAD_REF master
)

vcpkg_apply_patches(
    SOURCE_PATH ${SOURCE_PATH}
    PATCHES
        "${CMAKE_CURRENT_LIST_DIR}/fix-msvc-build.patch"
)

file(COPY ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt DESTINATION ${SOURCE_PATH})

if(WIN32)
    file(COPY ${CMAKE_CURRENT_LIST_DIR}/fuzzy.def DESTINATION ${SOURCE_PATH})
endif()

file(
    COPY ${CMAKE_CURRENT_LIST_DIR}/config.h.in.cmake
    DESTINATION ${SOURCE_PATH}
)

vcpkg_configure_cmake(
    SOURCE_PATH ${SOURCE_PATH}
    PREFER_NINJA
    OPTIONS_RELEASE -DINSTALL_HEADERS=ON
)

vcpkg_install_cmake()

vcpkg_fixup_cmake_targets(CONFIG_PATH share/ssdeep)

vcpkg_copy_pdbs()

file(
    INSTALL ${CMAKE_CURRENT_LIST_DIR}/License.txt
    DESTINATION ${CURRENT_PACKAGES_DIR}/share/ssdeep
    RENAME copyright
)

vcpkg_test_cmake(PACKAGE_NAME ssdeep)
