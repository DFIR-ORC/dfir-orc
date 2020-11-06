#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
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

if(ORC_USE_STATIC_CRT)
    set(STATIC_SUFFIX "_static")
endif()

find_library(UTF8PROC_LIB_DEBUG NAMES utf8proc${STATIC_SUFFIX})
find_library(UTF8PROC_LIB_RELEASE
    NAMES utf8proc${STATIC_SUFFIX}
    PATHS ${RELEASE_LIBRARIES_PATH}
    PATH_SUFFIXES lib
    NO_DEFAULT_PATH
)

add_library(utf8proc::utf8proc INTERFACE IMPORTED)

target_link_libraries(utf8proc::utf8proc
    INTERFACE
        debug ${UTF8PROC_LIB_DEBUG} optimized ${UTF8PROC_LIB_RELEASE}
)
