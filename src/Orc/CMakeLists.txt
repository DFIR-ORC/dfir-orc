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

find_package(VisualStudio REQUIRED)

set(GENERATED_SRC_DIR "${CMAKE_CURRENT_BINARY_DIR}/src")

configure_file(
    "Icon.h.in"
    "${GENERATED_SRC_DIR}/Icon.h"
    @ONLY
)

set(SRC
    "Orc.cpp"
    "Orc.rc"
    "targetver.h"
    "resource.h"
    "${GENERATED_SRC_DIR}/Icon.h"
    "Compatibility.manifest"
)

if(${TARGET_ARCH} STREQUAL "x86")
    list(APPEND SRC "xpcrtwrap.asm" "XPSP2Compatibility.cpp")
endif()

add_executable(Orc
    ${SRC}
)

target_include_directories(Orc
    PRIVATE
        ${GENERATED_SRC_DIR}
)

target_link_libraries(Orc
    PRIVATE
        OrcCommand
        OrcLib
        setupapi.lib
)

set_property(TARGET Orc PROPERTY FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}")
set_property(TARGET Orc PROPERTY OUTPUT_NAME "DFIR-ORC_${TARGET_ARCH}")
set_property(TARGET Orc APPEND PROPERTY LINK_FLAGS "/DEBUG /MANIFESTUAC:\"level='requireAdministrator'\"" )

foreach(CONFIG Debug MinSizeRel Release RelWithDebInfo)
    install(TARGETS Orc
        CONFIGURATIONS ${CONFIG}
        DESTINATION ${CONFIG}
    )

    install(FILES $<TARGET_PDB_FILE:Orc>
        CONFIGURATIONS ${CONFIG}
        DESTINATION ${CONFIG} OPTIONAL
    )
endforeach()
