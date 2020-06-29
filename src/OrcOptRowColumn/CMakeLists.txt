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

set(SRC "OrcOptRowColumn.cpp")

set(SRC_COMMON "dllmain.cpp" "targetver.h")
source_group(Common FILES ${SRC_COMMON} "stdafx.cpp" "stdafx.h")

add_library(OrcOptRowColumn
    SHARED
        "stdafx.h"
        "stdafx.cpp"
        ${SRC}
        ${SRC_COMMON}
)

target_link_libraries(OrcOptRowColumn
    PUBLIC
        OrcOptRowColumnLib
)

set_target_properties(OrcOptRowColumn
    PROPERTIES
        FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}OrcOptRowColumn"
)

foreach(CONFIG Debug MinSizeRel Release RelWithDebInfo)
    install(TARGETS OrcApacheOrc
        CONFIGURATIONS ${CONFIG}
        DESTINATION bin/${CONFIG}
    )

    install(FILES $<TARGET_PDB_FILE:OrcApacheOrc>
        CONFIGURATIONS ${CONFIG}
        DESTINATION pdb/${CONFIG} OPTIONAL
    )
endforeach()
