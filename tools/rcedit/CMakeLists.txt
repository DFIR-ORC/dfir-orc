#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#

cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(rcedit)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(ORC_USE_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_compile_options(/EHsc)

list(APPEND COMPILE_OPTIONS_RELEASE
    /guard:cf
)

foreach(OPTION IN ITEMS ${COMPILE_OPTIONS_DEBUG})
    add_compile_options($<$<CONFIG:DEBUG>:${OPTION}>)
endforeach()

foreach(OPTION IN ITEMS ${COMPILE_OPTIONS_RELEASE})
    add_compile_options($<$<CONFIG:RELEASE>:${OPTION}>)
    add_compile_options($<$<CONFIG:MINSIZEREL>:${OPTION}>)
    add_compile_options($<$<CONFIG:RELWITHDEBINFO>:${OPTION}>)
endforeach()

find_package(CLI11 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(7zip CONFIG REQUIRED)

set(SOURCES
    src/main.cpp
    src/cmd/cmd.h
    src/cmd/validators.h
    src/cmd/validators.cpp
    src/cmd/list/cmd_list.h
    src/cmd/list/cmd_list.cpp
    src/cmd/list/resources.h
    src/cmd/list/resources.cpp
    src/cmd/extract/cmd_extract.h
    src/cmd/extract/cmd_extract.cpp
    src/cmd/set/compression_type.h
    src/cmd/set/compression_type.cpp
    src/cmd/set/cmd_set.h
    src/cmd/set/cmd_set.cpp
    src/cmd/set/7z/archive_update_callback.h
    src/cmd/set/7z/archive_update_callback.cpp
    src/cmd/set/7z/compress_7z.h
    src/cmd/set/7z/compress_7z.cpp
    src/cmd/set/7z/in_mem_stream.h
    src/cmd/set/7z/in_mem_stream.cpp
    src/cmd/set/7z/out_mem_stream.h
    src/cmd/set/7z/out_mem_stream.cpp
    src/utils/guard.h
    src/utils/iconv.h
    src/utils/iconv.cpp
    src/utils/init_guid.cpp
)

add_executable(rcedit EXCLUDE_FROM_ALL
    ${SOURCES}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES ${SOURCES})

set_target_properties(rcedit
    PROPERTIES
        FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}/Tools"
)

target_compile_definitions(rcedit
    PRIVATE
        UNICODE
        NOMINMAX
)

target_include_directories(rcedit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(rcedit
    PRIVATE
        7zip::7zip
        7zip::extras
        CLI11::CLI11
        spdlog::spdlog
)
