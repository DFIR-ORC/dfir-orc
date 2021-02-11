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
    "ConfigFile_OrcConfig.cpp"
    "ConfigFile_OrcConfig.h"
)

source_group(Common FILES ${SRC_COMMON})

set(SRC_LOG
    "Log/UtilitiesLogger.cpp"
    "Log/UtilitiesLogger.h"
    "Log/UtilitiesLoggerConfiguration.h"
    "Log/UtilitiesLoggerConfiguration.cpp"
)

source_group(Log FILES ${SRC_LOG})

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

set(SRC_GETSECTORS
    "Command/GetSectors/GetSectors.h"
    "Command/GetSectors/GetSectorsSchema.xml"
    "Command/GetSectors/GetSectors_Config.cpp"
    "Command/GetSectors/GetSectors_Output.cpp"
    "Command/GetSectors/GetSectors_Run.cpp"
    "Command/GetSectors/GetSectorsSchema.xml"
)

source_group(GetSectors FILES ${SRC_GETSECTORS})

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

set(SRC_NTFSUTIL
    "Command/NTFSUtil/NTFSUtil.h"
    "Command/NTFSUtil/NTFSUtilSqlSchema.xml"
    "Command/NTFSUtil/NTFSUtil_Config.cpp"
    "Command/NTFSUtil/NTFSUtil_Output.cpp"
    "Command/NTFSUtil/NTFSUtil_Run.cpp"
    "Command/NTFSUtil/NTFSUtilSqlSchema.xml"
)

source_group(NTFSUtil FILES ${SRC_NTFSUTIL})

set(SRC_OBJINFO
    "Command/ObjInfo/ObjInfo.h"
    "Command/ObjInfo/ObjInfoSqlSchema.xml"
    "Command/ObjInfo/ObjInfo_Config.cpp"
    "Command/ObjInfo/ObjInfo_Output.cpp"
    "Command/ObjInfo/ObjInfo_Run.cpp"
    "Command/ObjInfo/ObjInfoSqlSchema.xml"
)

source_group(ObjInfo FILES ${SRC_OBJINFO})

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

set(SRC_TOOLEMBED
    "Command/ToolEmbed/ConfigFile_ToolEmbed.cpp"
    "Command/ToolEmbed/ConfigFile_ToolEmbed.h"
    "Command/ToolEmbed/ToolEmbed.h"
    "Command/ToolEmbed/ToolEmbed_Config.cpp"
    "Command/ToolEmbed/ToolEmbed_Output.cpp"
    "Command/ToolEmbed/ToolEmbed_Run.cpp"
)

source_group(ToolEmbed FILES ${SRC_TOOLEMBED})

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

set(SRC_WOLFLAUNCHER
    "Command/WolfLauncher/Console/ConsoleConfiguration.h"
    "Command/WolfLauncher/Console/ConsoleConfiguration.cpp"
    "Command/WolfLauncher/Console/Stream/WideStreambufConverter.cpp"
    "Command/WolfLauncher/Console/Stream/WideStreambufConverter.h"
    "Command/WolfLauncher/Console/Stream/UncheckedStreambuf.h"
    "Command/WolfLauncher/Console/Stream/UncheckedStreambuf.cpp"
    "Command/WolfLauncher/Console/Stream/StreamRedirector.cpp"
    "Command/WolfLauncher/Console/Stream/StreamRedirector.h"
    "Command/WolfLauncher/Console/Stream/Tee.cpp"
    "Command/WolfLauncher/Console/Stream/Tee.h"
    "Command/WolfLauncher/Console/Stream/LazyFileStreambuf.cpp"
    "Command/WolfLauncher/Console/Stream/LazyFileStreambuf.h"
    "Command/WolfLauncher/Console/Stream/LazyFileStream.cpp"
    "Command/WolfLauncher/Console/Stream/LazyFileStream.h"
    "Command/WolfLauncher/Console/Stream/StandardOutputRedirection.cpp"
    "Command/WolfLauncher/Console/Stream/StandardOutputRedirection.h"
    "Command/WolfLauncher/ConfigFile_WOLFLauncher.cpp"
    "Command/WolfLauncher/ConfigFile_WOLFLauncher.h"
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
)

source_group(WolfLauncher FILES ${SRC_WOLFLAUNCHER})

add_library(OrcCommand
    STATIC
        ${SRC}
        ${SRC_COMMON}
        ${SRC_CONSOLE}
        ${SRC_DD}
        ${SRC_FASTFIND}
        ${SRC_FATINFO}
        ${SRC_GETSAMPLES}
        ${SRC_GETSECTORS}
        ${SRC_GETTHIS}
        ${SRC_LOG}
        ${SRC_MOTHERSHIP}
        ${SRC_NTFSINFO}
        ${SRC_NTFSUTIL}
        ${SRC_OBJINFO}
        ${SRC_REGINFO}
        ${SRC_TOOLEMBED}
        ${SRC_USNINFO}
        ${SRC_WOLFLAUNCHER}
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
