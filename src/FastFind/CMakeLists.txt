#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

include(${ORC_ROOT}/cmake/Orc.cmake)
orc_add_compile_options()

find_package(Boost REQUIRED)
find_package(VisualStudio REQUIRED)

set(GENERATED_SRC_DIR "${CMAKE_CURRENT_BINARY_DIR}/src")

configure_file(
    "Icon.h.in"
    "${GENERATED_SRC_DIR}/Icon.h"
    @ONLY
)

set(SRC
    "FastFind.cpp"
    "targetver.h"
    "${GENERATED_SRC_DIR}/Icon.h"
)

add_executable(FastFind
    "stdafx.h"
    ${SRC}
    "FastFind.rc"
	"Compatibility.manifest"
)

target_include_directories(FastFind
    PRIVATE
        ${Boost_INCLUDE_DIRS}
        ${GENERATED_SRC_DIR}
)

target_link_libraries(FastFind
    PRIVATE
        OrcCommand
        crypt32.lib
)

set_target_properties(FastFind
    PROPERTIES
        OUTPUT_NAME "FastFind_${TARGET_ARCH}"
        FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}"
        LINK_FLAGS "/DEBUG /MANIFESTUAC:\"level='requireAdministrator'\""
)

target_precompile_headers(FastFind PRIVATE stdafx.h)

foreach(CONFIG Debug MinSizeRel Release RelWithDebInfo)
    install(TARGETS FastFind
        CONFIGURATIONS ${CONFIG}
        DESTINATION ${CONFIG}
    )

    install(FILES $<TARGET_PDB_FILE:FastFind>
        CONFIGURATIONS ${CONFIG}
        DESTINATION ${CONFIG} OPTIONAL
    )
endforeach()
