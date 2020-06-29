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
    "SqlConnection.h"
    "SqlWriter.h"
    "OrcSql.h"
    "OrcSql.cpp"
    "SqlConnection.cpp"
    "SqlWriter.cpp"
)

set(SRC_UTILITIES
    "dllmain.cpp"
    "targetver.h"
)

source_group(Utilities FILES ${SRC_UTILITIES} "stdafx.cpp" "stdafx.h")

add_library(OrcSql
    SHARED
        "stdafx.h"
        "stdafx.cpp"
        ${SRC}
        ${SRC_UTILITIES}
)

target_include_directories(OrcSql
    PRIVATE
        ${Boost_INCLUDE_DIRS}
        ${ODBC_INCLUDE_DIRS}
        "c:\\Program Files\\Microsoft SQL Server\\110\\SDK\\Include"
)

target_link_libraries(OrcSql
    PUBLIC
        OrcLib
        odbc32.lib
)

set_target_properties(OrcSql PROPERTIES FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}Sql")

foreach(CONFIG Debug MinSizeRel Release RelWithDebInfo)
    install(TARGETS OrcSql
        CONFIGURATIONS ${CONFIG}
        DESTINATION bin/${CONFIG}
    )

    install(FILES $<TARGET_PDB_FILE:OrcSql>
        CONFIGURATIONS ${CONFIG}
        DESTINATION pdb/${CONFIG} OPTIONAL
    )
endforeach()
