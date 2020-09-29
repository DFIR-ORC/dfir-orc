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
    "stdafx.cpp"
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
        VisualStudio::atls
)

set_target_properties(FastFind
    PROPERTIES
        OUTPUT_NAME "FastFind_${TARGET_ARCH}"
        FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}"
        LINK_FLAGS "/DEBUG /MANIFESTUAC:\"level='requireAdministrator'\""
)

target_precompile_headers(FastFind PRIVATE stdafx.h)

if("${CMAKE_GENERATOR_TOOLSET}" STREQUAL "v141_xp"
    AND "${TARGET_ARCH}" STREQUAL "x86")

    if(NOT EXISTS "${ORC_XMLLITE_PATH}")
        # The xmllite.dll is only available on an enoughly patched Windows XP SP2
        message(WARNING "${ORC_XMLLITE_PATH}: not found, it could be required on some XP SP2 installation")
    else()
        add_custom_command(
            TARGET FastFind
            POST_BUILD
            DEPENDS rcedit

            COMMAND $<TARGET_FILE:rcedit>
                set --type "VALUES"
                    --name "XMLLITE_X86DLL"
                    --value-utf16 "7z:#XMLLITE_X86_XPSP2^|xmllite.dll"
                    $<TARGET_FILE:FastFind>

            COMMAND $<TARGET_FILE:rcedit>
                set --type "BINARY"
                    --name "XMLLITE_X86_XPSP2"
                    --value-path "${ORC_XMLLITE_PATH}"
                    --compress=7z
                    $<TARGET_FILE:FastFind>
        )
    endif()
endif()

foreach(CONFIG Debug MinSizeRel Release RelWithDebInfo)
    install(TARGETS FastFind
        CONFIGURATIONS ${CONFIG}
        DESTINATION bin/${CONFIG}
    )

    install(FILES $<TARGET_PDB_FILE:FastFind>
        CONFIGURATIONS ${CONFIG}
        DESTINATION pdb/${CONFIG} OPTIONAL
    )
endforeach()
