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

target_precompile_headers(OrcSqlTest PRIVATE stdafx.h)

target_link_libraries(OrcSqlTest PRIVATE OrcSql)

set_target_properties(OrcSqlTest
    PROPERTIES
        FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}Sql"
)

if("${TARGET_ARCH}" STREQUAL "x64")
    set(ORCSQL_VAR_NAME "ORCSQL_X64DLL")
elseif("${TARGET_ARCH}" STREQUAL "x86")
    set(ORCSQL_VAR_NAME "ORCSQL_X86DLL")
else()
    message(FATAL_ERROR "Unknown architecture: ${TARGET_ARCH}")
endif()


add_custom_command(
    TARGET OrcSqlTest
    POST_BUILD
    DEPENDS rcedit, OrcSql
    VERBATIM

    COMMAND $<TARGET_FILE:rcedit>
        set --type "VALUES"
            --name "${ORCSQL_VAR_NAME}"
            --value-utf16 "7z:#ORCSQL|OrcSql.dll"
            $<TARGET_FILE:OrcSqlTest>

    COMMAND $<TARGET_FILE:rcedit>
        set --type "BINARY"
            --name "ORCSQL"
            --value-path $<TARGET_FILE:OrcSql>
            --compress=7z
            $<TARGET_FILE:OrcSqlTest>
)
