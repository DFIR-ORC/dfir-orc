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
    "ConfigFile_DD.cpp"
    "ConfigFile_DD.h"
    "DD.h"
    "DD_Config.cpp"
    "DD_Output.cpp"
    "DD_Run.cpp"
    "DDSqlSchema.xml"
)

source_group(DD FILES ${SRC_DD})

set(SRC_FASTFIND
    "ConfigFile_FastFind.cpp"
    "ConfigFile_FastFind.h"
    "FastFind.h"
    "FastFindSqlSchema.xml"
    "FastFind_Config.cpp"
    "FastFind_Output.cpp"
    "FastFind_Run.cpp"
    "FastFindSqlSchema.xml"
)

source_group(FastFind FILES ${SRC_FASTFIND})

set(SRC_FATINFO
    "ConfigFile_FatInfo.cpp"
    "ConfigFile_FatInfo.h"
    "FatInfo.h"
    "FatInfoSqlSchema.xml"
    "FatInfo_Config.cpp"
    "FatInfo_Output.cpp"
    "FatInfo_Run.cpp"
    "FatInfoSqlSchema.xml"
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
    "GetSectors.h"
    "GetSectorsSchema.xml"
    "GetSectors_Config.cpp"
    "GetSectors_Output.cpp"
    "GetSectors_Run.cpp"
    "GetSectorsSchema.xml"
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

set(SRC_MOTHERSHIP
    "Mothership.h"
    "Mothership_Config.cpp"
    "Mothership_Output.cpp"
    "Mothership_Run.cpp"
    "ConfigFile_OrcConfig.cpp"
    "ConfigFile_OrcConfig.h"
)

source_group(MotherShip FILES ${SRC_MOTHERSHIP})

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
    "NTFSUtil.h"
    "NTFSUtilSqlSchema.xml"
    "NTFSUtil_Config.cpp"
    "NTFSUtil_Output.cpp"
    "NTFSUtil_Run.cpp"
    "NTFSUtilSqlSchema.xml"
)

source_group(NTFSUtil FILES ${SRC_NTFSUTIL})

set(SRC_OBJINFO
    "ObjInfo.h"
    "ObjInfoSqlSchema.xml"
    "ObjInfo_Config.cpp"
    "ObjInfo_Output.cpp"
    "ObjInfo_Run.cpp"
    "ObjInfoSqlSchema.xml"
)

source_group(ObjInfo FILES ${SRC_OBJINFO})

set(SRC_REGINFO
    "ConfigFile_RegInfo.cpp"
    "ConfigFile_RegInfo.h"
    "RegInfo.h"
    "RegInfoSqlSchema.xml"
    "RegInfo_Config.cpp"
    "RegInfo_Output.cpp"
    "RegInfo_Run.cpp"
    "RegInfoSqlSchema.xml"
)

source_group(RegInfo FILES ${SRC_REGINFO})

set(SRC_TOOLEMBED
    "ConfigFile_ToolEmbed.cpp"
    "ConfigFile_ToolEmbed.h"
    "ToolEmbed.h"
    "ToolEmbed_Config.cpp"
    "ToolEmbed_Output.cpp"
    "ToolEmbed_Run.cpp"
)

source_group(ToolEmbed FILES ${SRC_TOOLEMBED})

set(SRC_USNINFO
    "ConfigFile_USNInfo.cpp"
    "ConfigFile_USNInfo.h"
    "USNInfo.h"
    "USNInfoSqlSchema.xml"
    "USNInfo_Config.cpp"
    "USNInfo_Output.cpp"
    "USNInfo_Run.cpp"
    "USNInfoSqlSchema.xml"
)

source_group(USNInfo FILES ${SRC_USNINFO})

set(SRC_WOLFLAUNCHER
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
