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
find_package(Parquet REQUIRED)

set(SRC
    "ParquetStream.h"
    "ParquetWriter.h"
    "ParquetDefinitions.h"
    "ParquetStream.cpp"
    "ParquetWriter.cpp"
)

set(SRC_COMMON "targetver.h")
source_group(Common FILES ${SRC_COMMON} "stdafx.h")

add_library(OrcParquetLib
    STATIC
        "stdafx.h"
        ${SRC}
        ${SRC_COMMON}
)

target_precompile_headers(OrcParquetLib PRIVATE stdafx.h)

target_compile_definitions(OrcParquetLib PUBLIC ARROW_STATIC PARQUET_STATIC)

target_include_directories(OrcParquetLib PRIVATE ${Boost_INCLUDE_DIRS})

target_include_directories(OrcParquetLib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(OrcParquetLib
    PUBLIC
        Parquet::Parquet
        OrcLib
)

set_target_properties(OrcParquetLib
    PROPERTIES
        FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}OrcParquet"
)
