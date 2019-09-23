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

set(SRC
    "resource.h"
    "sql_output.cpp"
)

set(SRC_COMMON
    "OrcSqlTest.cpp"
    "targetver.h"
)

source_group(Common FILES ${SRC_COMMON} "stdafx.cpp" "stdafx.h")

add_library(OrcSqlTest
    SHARED
        "stdafx.cpp"
        "stdafx.h"
        ${SRC}
        ${SRC_COMMON}
)

target_link_libraries(OrcSqlTest PRIVATE OrcSql)

set_target_properties(OrcSqlTest
    PROPERTIES
        FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}Sql"
)
