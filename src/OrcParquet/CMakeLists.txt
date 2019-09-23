#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

include(${ORC_ROOT}/CMake/Orc.cmake)
orc_add_compile_options()

find_package(boost REQUIRED)

set(SRC "OrcParquet.cpp")

set(SRC_COMMON "dllmain.cpp" "targetver.h")
source_group(Common FILES ${SRC_COMMON} "stdafx.cpp" "stdafx.h")

add_library(OrcParquet
    SHARED
        "stdafx.h"
        "stdafx.cpp"
        ${SRC}
        ${SRC_COMMON}
)

target_include_directories(OrcParquet PRIVATE ${Boost_INCLUDE_DIRS})

target_link_libraries(OrcParquet PRIVATE OrcParquetLib)

set_target_properties(OrcParquet
    PROPERTIES
        FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}OrcParquet"
)
