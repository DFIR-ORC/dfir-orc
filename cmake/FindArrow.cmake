#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

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

find_package(brotli REQUIRED)
find_package(BZip2 REQUIRED)
find_package(double-conversion REQUIRED)
find_package(Snappy CONFIG REQUIRED)
find_package(thrift REQUIRED)
find_package(LZ4 REQUIRED)
find_package(utf8proc REQUIRED)
find_package(ZLIB REQUIRED)
find_package(zstd CONFIG REQUIRED)

find_library(ARROW_LIB_DEBUG NAMES arrow)

find_library(ARROW_LIB_RELEASE
    NAMES arrow
    PATHS ${RELEASE_LIBRARIES_PATH}
    PATH_SUFFIXES lib
    NO_DEFAULT_PATH
)

add_library(Arrow::Arrow INTERFACE IMPORTED)

target_link_libraries(Arrow::Arrow
    INTERFACE
        brotli::brotli
        BZip2::BZip2
        double-conversion::double-conversion
        LZ4::LZ4
        Snappy::snappy
        thrift::thrift
        utf8proc::utf8proc
        ZLIB::ZLIB
        zstd::libzstd_static
        debug ${ARROW_LIB_DEBUG} optimized ${ARROW_LIB_RELEASE}
)
