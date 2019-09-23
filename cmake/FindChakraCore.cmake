#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

# Only dynamic library is available so enforce lookup in the matching vcpkg
# install directories
set(VCPKG_TRIPLET_DIR "${ORC_VCPKG_ROOT}/installed/${TARGET_ARCH}-windows")

find_path(CHAKRACORE_INCLUDE_DIR
    NAMES ChakraCommon.h
    PATHS ${VCPKG_TRIPLET_DIR}/include
)

find_library(CHAKRACORE_LIB_DEBUG
    NAMES chakracore
    PATHS ${VCPKG_TRIPLET_DIR}/debug/lib
)

find_library(CHAKRACORE_LIB_RELEASE
    NAMES chakracore
    PATHS ${VCPKG_TRIPLET_DIR}/lib
)

find_file(CHAKRACORE_DLL_DEBUG
    NAMES ChakraCore.dll
    PATHS ${VCPKG_TRIPLET_DIR}/debug/bin
)

find_file(CHAKRACORE_DLL_RELEASE
    NAMES ChakraCore.dll
    PATHS ${VCPKG_TRIPLET_DIR}/bin
)

find_file(CHAKRACORE_DLL_X86_DEBUG
    NAMES ChakraCore.dll
    PATHS ${ORC_VCPKG_ROOT}/installed/x86-windows/debug/bin
)

find_file(CHAKRACORE_DLL_X86_RELEASE
    NAMES ChakraCore.dll
    PATHS ${ORC_VCPKG_ROOT}/installed/x86-windows/bin
)

find_file(CHAKRACORE_DLL_X64_DEBUG
    NAMES ChakraCore.dll
    PATHS ${ORC_VCPKG_ROOT}/installed/x64-windows/debug/bin
)

find_file(CHAKRACORE_DLL_X64_RELEASE
    NAMES ChakraCore.dll
    PATHS ${ORC_VCPKG_ROOT}/installed/x64-windows/bin
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VisualStudio
    DEFAULT_MSG
    CHAKRACORE_INCLUDE_DIR
    CHAKRACORE_LIB_DEBUG
    CHAKRACORE_LIB_RELEASE
    CHAKRACORE_DLL_DEBUG
    CHAKRACORE_DLL_RELEASE
)

if(NOT TARGET chakracore::chakracore)
    add_library(chakracore::chakracore INTERFACE IMPORTED)

    target_include_directories(chakracore::chakracore
        INTERFACE
            ${CHAKRACORE_INCLUDE_DIR}
    )

    target_link_libraries(chakracore::chakracore
        INTERFACE
            debug ${CHAKRACORE_LIB_DEBUG}
            optimized ${CHAKRACORE_LIB_RELEASE}
    )
endif()

if(NOT TARGET chakracore::chakracore-header-only)
    add_library(chakracore::chakracore-header-only INTERFACE IMPORTED)

    target_include_directories(chakracore::chakracore-header-only
        INTERFACE
            ${CHAKRACORE_INCLUDE_DIR}
    )
endif()
