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
    "OptRowColumnMemoryPool.h"
    "OptRowColumnStream.h"
    "OptRowColumnWriter.h"
    "OptRowColumnMemoryPool.cpp"
    "OptRowColumnStream.cpp"
    "OptRowColumnWriter.cpp"
)

set(SRC_COMMON "targetver.h")

source_group(Common FILES ${SRC_COMMON} "stdafx.cpp" "stdafx.h")

add_library(OrcOptRowColumnLib
    STATIC
        "stdafx.h"
        "stdafx.cpp"
        ${SRC}
        ${SRC_COMMON}
)

target_include_directories(OrcOptRowColumnLib
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(OrcOptRowColumnLib
    PUBLIC
        crypt32.lib
        ApacheOrc::ApacheOrc
        OrcLib
)

set_target_properties(OrcOptRowColumnLib
    PROPERTIES
        FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}OrcOptRowColumn"
)
