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

find_package(VisualStudio REQUIRED)

set(SRC
    "orc_output.cpp"
    "OrcApacheOrcTest.cpp"
)

set(SRC_COMMON
    "resource.h"
    "targetver.h"
)

source_group(Common FILES ${SRC_COMMON} "stdafx.cpp" "stdafx.h")

add_library(OrcApacheOrcTest
    SHARED
        "stdafx.cpp"
        "stdafx.h"
        ${SRC}
        ${SRC_COMMON}
)

target_link_libraries(OrcApacheOrcTest
    PRIVATE
        VisualStudio::CppUnitTest
        OrcApacheOrcLib
)

set_target_properties(OrcApacheOrcTest
    PROPERTIES
        FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}OrcApacheOrc"
)

target_precompile_headers(OrcApacheOrcTest PRIVATE stdafx.h)

if("${TARGET_ARCH}" STREQUAL "x64")
    set(APACHEORC_VAR_NAME "APACHEORC_X64DLL")
elseif("${TARGET_ARCH}" STREQUAL "x86")
    set(APACHEORC_VAR_NAME "APACHEORC_X86DLL")
else()
    message(FATAL_ERROR "Unknown architecture: ${TARGET_ARCH}")
endif()

add_custom_command(
    TARGET OrcApacheOrcTest
    POST_BUILD
    DEPENDS rcedit, OrcApacheOrc

    COMMAND $<TARGET_FILE:rcedit>
        set --type "VALUES"
            --name "${APACHEORC_VAR_NAME}"
            --value-utf16 "7z:#APACHEORC^|OrcApacheOrc.dll"
            $<TARGET_FILE:OrcApacheOrcTest>

    COMMAND $<TARGET_FILE:rcedit>
        set --type "BINARY"
            --name "APACHEORC"
            --value-path $<TARGET_FILE:OrcApacheOrc>
            --compress=7z
            $<TARGET_FILE:OrcApacheOrcTest>
)
