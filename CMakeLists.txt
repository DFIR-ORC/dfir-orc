cmake_minimum_required(VERSION 3.12)
cmake_policy(VERSION 3.12)

# CMake user options
option(ORC_USE_STATIC_CRT "Use static runtime" ON)
option(ORC_BUILD_VCPKG "Build vcpkg dependencies" OFF)

option(ORC_BUILD_APACHE_ORC "Build Apache's ORC module" OFF)
option(ORC_BUILD_CHAKRACORE "Build with chakracore support" OFF)
option(ORC_BUILD_FASTFIND "Build FastFind binary" ON)
option(ORC_BUILD_ORC "Build Orc binary" ON)
option(ORC_BUILD_PARQUET "Build Parquet module" OFF)
option(ORC_BUILD_SQL "Build SQL module" OFF)
option(ORC_BUILD_SSDEEP "Build with ssdeep support" OFF)

set(ORC_VCPKG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/vcpkg"
    CACHE FILEPATH "VCPKG root directory"
)

set(ORC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# Insert custom cmake directories for script lookup
list(INSERT CMAKE_MODULE_PATH 0 ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Default target is 'Windows Seven' unless xp toolset is used
if(NOT CMAKE_SYSTEM_VERSION)
    if("${CMAKE_GENERATOR_TOOLSET}" STREQUAL "v141_xp")
        set(CMAKE_SYSTEM_VERSION 5.1)
    else()
        set(CMAKE_SYSTEM_VERSION 6.1)
    endif()
endif()

include(cmake/architecture.cmake)
get_target_architecture(RESULT_VARIABLE "TARGET_ARCH")

set(OVERLAY_TRIPLETS ${CMAKE_BINARY_DIR}/vcpkg/triplets)
include(cmake/vcpkg_configure_triplets.cmake)
vcpkg_configure_triplets(OUTPUT_TRIPLETS_DIR ${OVERLAY_TRIPLETS})

if(${TARGET_ARCH} STREQUAL "x86")
    set(ORC_BUILD_PARQUET OFF)
endif()

# Build the required package dependency list
if(ORC_VCPKG_ROOT)
    list(APPEND _PACKAGES
        7zip
        boost-algorithm
        boost-dynamic-bitset
        boost-format
        boost-logic
        boost-multi-index
        boost-scope-exit
        fmt
        tlsh
        yara
    )

    # Tools/rcedit specific dependencies
    list(APPEND _PACKAGES
        cli11
        spdlog
    )

    if(ORC_BUILD_APACHE_ORC)
        list(APPEND _PACKAGES orc)
    endif()

    if(ORC_BUILD_PARQUET)
        list(APPEND _PACKAGES arrow)
    endif()

    if(ORC_BUILD_CHAKRACORE)
        list(APPEND _PACKAGES
            chakracore:x86-windows
            chakracore:x64-windows
        )
    endif()

    if(ORC_BUILD_SSDEEP)
        add_definitions(-DORC_BUILD_SSDEEP)
        list(APPEND _PACKAGES ssdeep)
    endif()

    include(cmake/vcpkg.cmake)
    if(ORC_BUILD_VCPKG)
        vcpkg_install(
            PATH ${ORC_VCPKG_ROOT}
            PACKAGES ${_PACKAGES}
            ARCH ${TARGET_ARCH}
            USE_STATIC_CRT ${ORC_USE_STATIC_CRT}
            OVERLAY_TRIPLETS ${OVERLAY_TRIPLETS}
        )
    endif()
endif()

# Must be set AFTER 'VCPKG_TARGET_TRIPLET' is defined
project(dfir-orc LANGUAGES C CXX ASM_MASM)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Workaround for https://github.com/Microsoft/vcpkg/issues/5621
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release)
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)

if(NOT ORC_ROOT_VIRTUAL_FOLDER)
    set(ORC_ROOT_VIRTUAL_FOLDER "" CACHE STRING "Root virtual folder name")
else()
    set(ORC_ROOT_VIRTUAL_FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}/")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(tools)
