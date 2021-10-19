#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

find_package(protobuf CONFIG REQUIRED)
find_package(Snappy CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(LZ4 REQUIRED)
find_package(zstd CONFIG REQUIRED)

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

find_library(APACHE_ORC_LIB_DEBUG NAMES orc)

find_library(APACHE_ORC_LIB_RELEASE
    NAMES orc
    PATHS ${RELEASE_LIBRARIES_PATH}
    PATH_SUFFIXES lib
    NO_DEFAULT_PATH
)

add_library(ApacheOrc::ApacheOrc INTERFACE IMPORTED)

target_link_libraries(ApacheOrc::ApacheOrc
    INTERFACE
        protobuf::libprotoc
        protobuf::libprotobuf
        Snappy::snappy
        ZLIB::ZLIB
        LZ4::LZ4
        zstd::libzstd_static
        debug ${APACHE_ORC_LIB_DEBUG} optimized ${APACHE_ORC_LIB_RELEASE}
)
