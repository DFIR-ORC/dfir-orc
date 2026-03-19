cmake_minimum_required(VERSION 3.16)

# CMake user options
option(ORC_USE_STATIC_CRT "Use static runtime" ON)
option(ORC_BUILD_VCPKG "Build vcpkg dependencies" ON)

option(ORC_BUILD_APACHE_ORC "Build Apache's ORC module" OFF)
option(ORC_BUILD_FASTFIND   "Build FastFind binary" ON)
option(ORC_BUILD_ORC        "Build Orc binary" ON)
option(ORC_BUILD_PARQUET    "Build Parquet module" OFF)
option(ORC_BUILD_SSDEEP     "Build with ssdeep support" OFF)
option(ORC_BUILD_JSON       "Build with JSON StructuredOutput enabled" ON)
option(ORC_BUILD_BOOST_STACKTRACE  "Build with stack backtrace enabled" OFF)
option(ORC_BUILD_TEST       "Build tests" ON)
option(ORC_BUILD_COMMAND    "Build any OrcCommand based command" ON)
option(ORC_DOWNLOADS_ONLY   "Do not build ORC but only download vcpkg third parties" OFF)
option(ORC_DISABLE_PRECOMPILED_HEADERS "Disable precompiled headers" OFF)

option(ORC_BUILD_ORCLIB_ONLY "Build OrcLib only" OFF)
option(ORC_BUILD_CAPSULE_ONLY "Build OrcCapsule only" OFF)

if(NOT ORC_BUILD_COMMAND)
    set(ORC_BUILD_FASTFIND OFF)
    set(ORC_BUILD_ORC      OFF)
else()
    option(ORC_COMMAND_DD           "Add DD subcommand" OFF)
    option(ORC_COMMAND_FASTFIND     "Add FastFind subcommand" ON)
    option(ORC_COMMAND_FATINFO      "Add FatInfo subcommand" ON)
    option(ORC_COMMAND_GETSAMPLES   "Add GetSamples subcommand" ON)
    option(ORC_COMMAND_GETSECTORS   "Add GetSectors subcommand" ON)
    option(ORC_COMMAND_GETTHIS      "Add GetThis subcommand" ON)
    option(ORC_COMMAND_NTFSINFO     "Add NTFSInfo subcommand" ON)
    option(ORC_COMMAND_NTFSUTIL     "Add NTFSUtil subcommand" ON)
    option(ORC_COMMAND_OBJINFO      "Add ObjInfo subcommand" ON)
    option(ORC_COMMAND_REGINFO      "Add RegInfo subcommand" OFF)
    option(ORC_COMMAND_TOOLEMBED    "Add ToolEmbed subcommand" ON)
    option(ORC_COMMAND_USNINFO      "Add USNInfo subcommand" ON)
    option(ORC_COMMAND_WOLFLAUNCHER "Add WolfLauncher subcommand" ON)
endif()

set(ORC_ICON "${CMAKE_CURRENT_SOURCE_DIR}/res/DFIR-ORC.ico" CACHE STRING "Application icon")
set(ORC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(ORC_VCPKG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/vcpkg" CACHE FILEPATH "VCPKG root directory")

list(INSERT CMAKE_MODULE_PATH 0 ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(cmake/architecture.cmake)
get_target_architecture(RESULT_VARIABLE "TARGET_ARCH")

if(ORC_USE_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Default target is 'Windows Seven' unless xp toolset is used
if(NOT CMAKE_SYSTEM_VERSION)
    if("${CMAKE_GENERATOR_TOOLSET}" STREQUAL "v141_xp")
        if (TARGET_ARCH STREQUAL "x64")
            set(CMAKE_SYSTEM_VERSION 5.2)
        else()
            set(CMAKE_SYSTEM_VERSION 5.1)
        endif()

        set(CMAKE_CXX_STANDARD 17)
    else()
        set(CMAKE_SYSTEM_VERSION 6.1)
        set(CMAKE_CXX_STANDARD 23)
    endif()
endif()


if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${ORC_VCPKG_ROOT}\\scripts\\buildsystems\\vcpkg.cmake")
endif()

project(dfir-orc LANGUAGES C CXX ASM_MASM)

# Configure triplet dynamically with options chosen from CMake

# Setup repository related variables and force refresh on new commits
include(cmake/GitInfo.cmake)
git_monitor_status()
git_info(
    REPO_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    VERSION_FROM_SEMVER_TAG ORC_VERSION
    BRANCH ORC_BRANCH
    COMMIT ORC_COMMIT
)

# Serialization pull a lot of deps and breaks XP SP2 because of boost-thread's "GetLogicalProcessorInformation"
add_compile_definitions(
    BOOST_MULTI_INDEX_DISABLE_SERIALIZATION
    BOOST_MULTI_INDEX_DISABLE_COMPRESSED_ORDERED_INDEX_NODES
)

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

if (ORC_BUILD_ORCLIB_ONLY OR ORC_BUILD_CAPSULE_ONLY)
    return ()
endif()

add_subdirectory(tools)

if(ORC_BUILD_TEST)
    add_subdirectory(tests)
endif()
