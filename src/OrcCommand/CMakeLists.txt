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

set(GENERATED_SRC_DIR "${CMAKE_CURRENT_BINARY_DIR}/src")

set(ORC_META_NAME "" CACHE STRING "Product meta name")
set(ORC_META_VERSION "" CACHE STRING "Product meta version")

configure_file(
    ToolVersionRes.h.in
    "${GENERATED_SRC_DIR}/ToolVersionRes.h"
    @ONLY
)

set(SRC
    "UtilitiesMain.cpp"
    "UtilitiesMain.h"
    "UtilitiesMain_Config.cpp"
)

source_group("" FILES ${SRC})

set(SRC_COMMON
    "Console.h"
    "FileInfoCommon.cpp"
    "FileInfoCommon.h"
    "OrcCommand.rc"
    "OrcCommand.h"
    "stdafx.h"
    "targetver.h"
    "ToolVersion.cpp"
    "ToolVersion.h"
    "${GENERATED_SRC_DIR}/ToolVersionRes.h"
    "Usage.h"
    "Usage.cpp"
    "Configuration/ShadowsParserOption.h"
    "Configuration/ShadowsParserOption.cpp"
)

source_group(Common FILES ${SRC_COMMON})

set(SRC_LOG
    "Log/UtilitiesLogger.cpp"
    "Log/UtilitiesLogger.h"
    "Log/UtilitiesLoggerConfiguration.h"
    "Log/UtilitiesLoggerConfiguration.cpp"
)

source_group(Log FILES ${SRC_LOG})

set(SRC_COMMANDS "")

if(ORC_COMMAND_DD)
    set(SRC_DD
        "Command/DD/ConfigFile_DD.cpp"
        "Command/DD/ConfigFile_DD.h"
        "Command/DD/DD.h"
        "Command/DD/DD_Config.cpp"
        "Command/DD/DD_Output.cpp"
        "Command/DD/DD_Run.cpp"
        "Command/DD/DDSqlSchema.xml"
    )

    source_group(DD FILES ${SRC_DD})
    list(APPEND SRC_COMMANDS ${SRC_DD})
    add_compile_definitions(ORC_COMMAND_DD)
endif()

if(ORC_COMMAND_FASTFIND)
    set(SRC_FASTFIND
        "Command/FastFind/ConfigFile_FastFind.cpp"
        "Command/FastFind/ConfigFile_FastFind.h"
        "Command/FastFind/FastFind.h"
        "Command/FastFind/FastFindSqlSchema.xml"
        "Command/FastFind/FastFind_Config.cpp"
        "Command/FastFind/FastFind_Output.cpp"
        "Command/FastFind/FastFind_Run.cpp"
        "Command/FastFind/FastFindSqlSchema.xml"
    )
    source_group(FastFind FILES ${SRC_FASTFIND})
    list(APPEND SRC_COMMANDS ${SRC_FASTFIND})
    add_compile_definitions(ORC_COMMAND_FASTFIND)
endif()

if(ORC_COMMAND_FATINFO)
    set(SRC_FATINFO
        "Command/FatInfo/ConfigFile_FatInfo.cpp"
        "Command/FatInfo/ConfigFile_FatInfo.h"
        "Command/FatInfo/FatInfo.h"
        "Command/FatInfo/FatInfoSqlSchema.xml"
        "Command/FatInfo/FatInfo_Config.cpp"
        "Command/FatInfo/FatInfo_Output.cpp"
        "Command/FatInfo/FatInfo_Run.cpp"
        "Command/FatInfo/FatInfoSqlSchema.xml"
    )

    source_group(FatInfo FILES ${SRC_FATINFO})
    list(APPEND SRC_COMMANDS ${SRC_FATINFO})
    add_compile_definitions(ORC_COMMAND_FATINFO)
endif()

if(ORC_COMMAND_GETSAMPLES)
    set(SRC_GETSAMPLES
        "Command/GetSamples/ConfigFile_GetSamples.cpp"
        "Command/GetSamples/ConfigFile_GetSamples.h"
        "Command/GetSamples/GetSamples.h"
        "Command/GetSamples/GetSamplesSchema.xml"
        "Command/GetSamples/GetSamples_Config.cpp"
        "Command/GetSamples/GetSamples_Output.cpp"
        "Command/GetSamples/GetSamples_Run.cpp"
        "Command/GetSamples/GetSamplesSchema.xml"
    )

    source_group(GetSamples FILES ${SRC_GETSAMPLES})
    list(APPEND SRC_COMMANDS ${SRC_GETSAMPLES})
    add_compile_definitions(ORC_COMMAND_GETSAMPLES)
endif()

if(ORC_COMMAND_GETSECTORS)
    set(SRC_GETSECTORS
        "Command/GetSectors/GetSectors.h"
        "Command/GetSectors/GetSectorsSchema.xml"
        "Command/GetSectors/GetSectors_Config.cpp"
        "Command/GetSectors/GetSectors_Output.cpp"
        "Command/GetSectors/GetSectors_Run.cpp"
        "Command/GetSectors/GetSectorsSchema.xml"
    )

    source_group(GetSectors FILES ${SRC_GETSECTORS})
    list(APPEND SRC_COMMANDS ${SRC_GETSECTORS})
    add_compile_definitions(ORC_COMMAND_GETSECTORS)
endif()

if(ORC_COMMAND_GETTHIS)
    set(SRC_GETTHIS
        "Command/GetThis/ConfigFile_GetThis.cpp"
        "Command/GetThis/ConfigFile_GetThis.h"
        "Command/GetThis/GetThis.h"
        "Command/GetThis/GetThisSqlSchema.xml"
        "Command/GetThis/GetThis_Config.cpp"
        "Command/GetThis/GetThis_Output.cpp"
        "Command/GetThis/GetThis_Run.cpp"
        "Command/GetThis/GetThisSqlSchema.xml"
    )

    source_group(GetThis FILES ${SRC_GETTHIS})
    list(APPEND SRC_COMMANDS ${SRC_GETTHIS})
    add_compile_definitions(ORC_COMMAND_GETTHIS)
endif()

if(ORC_COMMAND_NTFSINFO)
    set(SRC_NTFSINFO
        "Command/NTFSInfo/ConfigFile_NTFSInfo.cpp"
        "Command/NTFSInfo/ConfigFile_NTFSInfo.h"
        "Command/NTFSInfo/NTFSInfo.h"
        "Command/NTFSInfo/NTFSInfoSample.xml"
        "Command/NTFSInfo/NTFSInfoSqlSchema.xml"
        "Command/NTFSInfo/NTFSInfo_Config.cpp"
        "Command/NTFSInfo/NTFSInfo_Output.cpp"
        "Command/NTFSInfo/NTFSInfo_Run.cpp"
        "Command/NTFSInfo/NTFSInfoSqlSchema.xml"
    )

    source_group(NTFSInfo FILES ${SRC_NTFSINFO})
    list(APPEND SRC_COMMANDS ${SRC_NTFSINFO})
    add_compile_definitions(ORC_COMMAND_NTFSINFO)
endif()

if(ORC_COMMAND_NTFSUTIL)
    set(SRC_NTFSUTIL
        "Command/NTFSUtil/NTFSUtil.h"
        "Command/NTFSUtil/NTFSUtilSqlSchema.xml"
        "Command/NTFSUtil/NTFSUtil_Config.cpp"
        "Command/NTFSUtil/NTFSUtil_Output.cpp"
        "Command/NTFSUtil/NTFSUtil_Run.cpp"
        "Command/NTFSUtil/NTFSUtilSqlSchema.xml"
    )

    source_group(NTFSUtil FILES ${SRC_NTFSUTIL})
    list(APPEND SRC_COMMANDS ${SRC_NTFSUTIL})
    add_compile_definitions(ORC_COMMAND_NTFSUTIL)
endif()

if(ORC_COMMAND_OBJINFO)
    set(SRC_OBJINFO
        "Command/ObjInfo/ObjInfo.h"
        "Command/ObjInfo/ObjInfoSqlSchema.xml"
        "Command/ObjInfo/ObjInfo_Config.cpp"
        "Command/ObjInfo/ObjInfo_Output.cpp"
        "Command/ObjInfo/ObjInfo_Run.cpp"
        "Command/ObjInfo/ObjInfoSqlSchema.xml"
    )

    source_group(ObjInfo FILES ${SRC_OBJINFO})
    list(APPEND SRC_COMMANDS ${SRC_OBJINFO})
    add_compile_definitions(ORC_COMMAND_OBJINFO)
endif()

if(ORC_COMMAND_REGINFO)
    set(SRC_REGINFO
        "Command/RegInfo/ConfigFile_RegInfo.cpp"
        "Command/RegInfo/ConfigFile_RegInfo.h"
        "Command/RegInfo/RegInfo.h"
        "Command/RegInfo/RegInfoSqlSchema.xml"
        "Command/RegInfo/RegInfo_Config.cpp"
        "Command/RegInfo/RegInfo_Output.cpp"
        "Command/RegInfo/RegInfo_Run.cpp"
        "Command/RegInfo/RegInfoSqlSchema.xml"
    )

    source_group(RegInfo FILES ${SRC_REGINFO})
    list(APPEND SRC_COMMANDS ${SRC_REGINFO})
    add_compile_definitions(ORC_COMMAND_REGINFO)
endif()

if(ORC_COMMAND_TOOLEMBED)
    set(SRC_TOOLEMBED
        "Command/ToolEmbed/ConfigFile_ToolEmbed.cpp"
        "Command/ToolEmbed/ConfigFile_ToolEmbed.h"
        "Command/ToolEmbed/ToolEmbed.h"
        "Command/ToolEmbed/ToolEmbed_Config.cpp"
        "Command/ToolEmbed/ToolEmbed_Output.cpp"
        "Command/ToolEmbed/ToolEmbed_Run.cpp"
    )

    source_group(ToolEmbed FILES ${SRC_TOOLEMBED})
    list(APPEND SRC_COMMANDS ${SRC_TOOLEMBED})
    add_compile_definitions(ORC_COMMAND_TOOLEMBED)
endif()

if(ORC_COMMAND_USNINFO)
    set(SRC_USNINFO
        "Command/USNInfo/ConfigFile_USNInfo.cpp"
        "Command/USNInfo/ConfigFile_USNInfo.h"
        "Command/USNInfo/USNInfo.h"
        "Command/USNInfo/USNInfoSqlSchema.xml"
        "Command/USNInfo/USNInfo_Config.cpp"
        "Command/USNInfo/USNInfo_Output.cpp"
        "Command/USNInfo/USNInfo_Run.cpp"
        "Command/USNInfo/USNInfoSqlSchema.xml"
    )

    source_group(USNInfo FILES ${SRC_USNINFO})
    list(APPEND SRC_COMMANDS ${SRC_USNINFO})
    add_compile_definitions(ORC_COMMAND_USNINFO)
endif()

if(ORC_COMMAND_WOLFLAUNCHER)
    set(SRC_WOLFLAUNCHER
        "Command/WolfLauncher/ConfigFile_WOLFLauncher.cpp"
        "Command/WolfLauncher/ConfigFile_WOLFLauncher.h"
        "Command/WolfLauncher/ConsoleConfiguration.h"
        "Command/WolfLauncher/ConsoleConfiguration.cpp"
        "Command/WolfLauncher/Journal.h"
        "Command/WolfLauncher/Outcome.h"
        "Command/WolfLauncher/Outcome.cpp"
        "Command/WolfLauncher/WolfExecution.cpp"
        "Command/WolfLauncher/WolfExecution.h"
        "Command/WolfLauncher/WolfExecution_Config.cpp"
        "Command/WolfLauncher/WolfExecution_Execute.cpp"
        "Command/WolfLauncher/WolfLauncher.h"
        "Command/WolfLauncher/WolfLauncher_Config.cpp"
        "Command/WolfLauncher/WolfLauncher_Output.cpp"
        "Command/WolfLauncher/WolfLauncher_Run.cpp"
        "Command/WolfLauncher/WolfTask.cpp"
        "Command/WolfLauncher/WolfTask.h"
        "Command/WolfLauncher/WolfLauncherSqlSchema.xml"
        "Text/Fmt/WolfPriority.h"
        "Text/Print/Recipient.h"
        "Text/Print/Recipient.cpp"
    )

    source_group(WolfLauncher FILES ${SRC_WOLFLAUNCHER})
    list(APPEND SRC_COMMANDS ${SRC_WOLFLAUNCHER})
    add_compile_definitions(ORC_COMMAND_WOLFLAUNCHER)
endif()

if(ORC_BUILD_ORC OR ORC_COMMAND_WOLFLAUNCHER) # Building Orc.exe (i.e. Mothership) or WolfLauncher require those files
    set(SRC_ORC_CONFIG
        "ConfigFile_OrcConfig.cpp"
        "ConfigFile_OrcConfig.h"
    )
    list(APPEND SRC_COMMANDS ${SRC_ORC_CONFIG})
endif()

add_library(OrcCommand
    STATIC
        ${SRC}
        ${SRC_LOG}
        ${SRC_COMMON}
        ${SRC_CONSOLE}
        ${SRC_COMMANDS}
)

target_sources(OrcCommand PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/OrcCommand.rc")

target_include_directories(OrcCommand
    PRIVATE
        ${Boost_INCLUDE_DIRS}
    PUBLIC
        ${GENERATED_SRC_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(OrcCommand PUBLIC OrcLib)

if(NOT ORC_DISABLE_PRECOMPILED_HEADERS)
    target_precompile_headers(OrcCommand PRIVATE stdafx.h)
endif()

set_target_properties(OrcCommand PROPERTIES FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}")
