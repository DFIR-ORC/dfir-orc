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

set(SRC_MOTHERSHIP
    "Mothership.h"
    "Mothership_Config.cpp"
    "Mothership_Output.cpp"
    "Mothership_Run.cpp"
)

source_group(MotherShip FILES ${SRC_MOTHERSHIP})

set(SRC
    "targetver.h"
    "resource.h"
    "Orc.cpp"
    "Compatibility.manifest"
    "Orc.rc"
    ${SRC_MOTHERSHIP}
    "${GENERATED_SRC_DIR}/Icon.h"
    )

if(${TARGET_ARCH} STREQUAL "x86")
    list(APPEND SRC "xpcrtwrap.asm" "XPSP2Compatibility.cpp")
endif()

add_executable(Orc
    "stdafx.h"
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

if(${ORC_SWAPRUN_NET})
    target_link_options(Orc PRIVATE "/SWAPRUN:NET")
endif()

target_precompile_headers(Orc PRIVATE stdafx.h)

if("${CMAKE_GENERATOR_TOOLSET}" STREQUAL "v141_xp"
    AND "${TARGET_ARCH}" STREQUAL "x86")

    if(NOT EXISTS "${ORC_XMLLITE_PATH}")
        # The xmllite.dll is only available on an enoughly patched Windows XP SP2
        message(WARNING "${ORC_XMLLITE_PATH}: not found, it could be required on some XP SP2 installation")
    else()
        add_custom_command(
            TARGET Orc
            POST_BUILD
            DEPENDS rcedit

            COMMAND $<TARGET_FILE:rcedit>
                set --type "VALUES"
                    --name "XMLLITE_X86DLL"
                    --value-utf16 "7z:#XMLLITE_X86_XPSP2^|xmllite.dll"
                    $<TARGET_FILE:Orc>

            COMMAND $<TARGET_FILE:rcedit>
                set --type "BINARY"
                    --name "XMLLITE_X86_XPSP2"
                    --value-path "${ORC_XMLLITE_PATH}"
                    --compress=7z
                    $<TARGET_FILE:Orc>
        )
    endif()
endif()

if(${ORC_BUILD_PARQUET})

    if("${TARGET_ARCH}" STREQUAL "x64")
        set(ORCPARQUET_VAR_NAME "ORCPARQUET_X64DLL")
    elseif("${TARGET_ARCH}" STREQUAL "x86")
        set(ORCPARQUET_VAR_NAME "ORCPARQUET_X86DLL")
    else()
        message(FATAL_ERROR "Unknown architecture: ${TARGET_ARCH}")
    endif()


    add_custom_command(
        TARGET Orc
        POST_BUILD
        DEPENDS rcedit, OrcParquet

        COMMAND $<TARGET_FILE:rcedit>
            set --type "VALUES"
                --name "${ORCPARQUET_VAR_NAME}"
                --value-utf16 "7z:#ORCPARQUET^|OrcParquet.dll"
                $<TARGET_FILE:Orc>

        COMMAND $<TARGET_FILE:rcedit>
            set --type "BINARY"
                --name "ORCPARQUET"
                --value-path $<TARGET_FILE:OrcParquet>
                --compress=7z
                $<TARGET_FILE:Orc>
    )
endif()

if(${ORC_BUILD_APACHE_ORC})

    if("${TARGET_ARCH}" STREQUAL "x64")
        set(APACHEORC_VAR_NAME "APACHEORC_X64DLL")
    elseif("${TARGET_ARCH}" STREQUAL "x86")
        set(APACHEORC_VAR_NAME "APACHEORC_X86DLL")
    else()
        message(FATAL_ERROR "Unknown architecture: ${TARGET_ARCH}")
    endif()

    add_custom_command(
        TARGET Orc
        POST_BUILD
        DEPENDS rcedit, OrcApacheOrc

        COMMAND $<TARGET_FILE:rcedit>
            set --type "VALUES"
                --name "${APACHEORC_VAR_NAME}"
                --value-utf16 "7z:#APACHEORC^|OrcApacheOrc.dll"
                $<TARGET_FILE:Orc>

        COMMAND $<TARGET_FILE:rcedit>
            set --type "BINARY"
                --name "APACHEORC"
                --value-path $<TARGET_FILE:OrcApacheOrc>
                --compress=7z
                $<TARGET_FILE:Orc>
    )
endif()

set_property(TARGET Orc PROPERTY FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}")
set_property(TARGET Orc PROPERTY OUTPUT_NAME "DFIR-Orc_${TARGET_ARCH}")
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
