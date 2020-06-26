#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

include(${ORC_ROOT}/cmake/Orc.cmake)
orc_add_compile_options()

find_package(ApacheOrc REQUIRED)

set(SRC
    "ApacheOrcMemoryPool.h"
    "ApacheOrcStream.h"
    "ApacheOrcWriter.h"
    "ApacheOrcMemoryPool.cpp"
    "ApacheOrcStream.cpp"
    "ApacheOrcWriter.cpp"
)

set(SRC_COMMON "targetver.h")

source_group(Common FILES ${SRC_COMMON} "stdafx.cpp" "stdafx.h")

add_library(OrcApacheOrcLib
    STATIC
        "stdafx.h"
        "stdafx.cpp"
        ${SRC}
        ${SRC_COMMON}
)

target_include_directories(OrcApacheOrcLib
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(OrcApacheOrcLib
    PUBLIC
        crypt32.lib
        ApacheOrc::ApacheOrc
        OrcLib
)

set_target_properties(OrcApacheOrcLib
    PROPERTIES
        FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}OrcApacheOrc"
)
