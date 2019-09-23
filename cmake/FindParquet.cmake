#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

find_package(Arrow REQUIRED)
find_package(BoostRegex REQUIRED COMPONENTS regex)
find_package(BoostFilesystem REQUIRED COMPONENTS filesystem)

# Unfortunately the directory search order has 'debug' first:
#
# CMAKE_PREFIX_PATH=D:/vcpkg_2017/installed/x64-windows-static/debug;D:/vcpkg_2017/installed/x64-windows-static
#
# Some library built with VCPKG (like 'orc') have the same name for debug and
# release. As a workaround LIBRARIES_PATH will strip debug path from the
# CMAKE_PREFIX_PATH so it can be use to find the release version.
foreach(item ${CMAKE_PREFIX_PATH})
    string(REGEX MATCH ".*/debug$" match ${item})
    if(NOT match)
        list(APPEND RELEASE_LIBRARIES_PATH ${item})
    endif()
endforeach()

if(ORC_USE_STATIC_CRT)
    find_library(PARQUET_LIB_DEBUG NAMES parquet)

    find_library(PARQUET_LIB_RELEASE
        NAMES parquet
        PATHS ${RELEASE_LIBRARIES_PATH}
        PATH_SUFFIXES lib
        NO_DEFAULT_PATH
    )
else()
    find_library(PARQUET_LIB_DEBUG NAMES parquet)

    find_library(PARQUET_LIB_RELEASE
        NAMES parquet
        PATHS ${RELEASE_LIBRARIES_PATH}
        PATH_SUFFIXES lib
        NO_DEFAULT_PATH
    )
endif()

add_library(Parquet::Parquet INTERFACE IMPORTED)

target_link_libraries(Parquet::Parquet
    INTERFACE
        Arrow::Arrow
        boost::regex
        boost::filesystem
        debug ${PARQUET_LIB_DEBUG} optimized ${PARQUET_LIB_RELEASE}
)
