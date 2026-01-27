cmake_minimum_required(VERSION 3.16)

project(dfir-orc-capsule)

get_filename_component(ORC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CAPSULE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(GENERATED_SRC_DIR "${CMAKE_CURRENT_BINARY_DIR}")

include(${ORC_ROOT}/cmake/architecture.cmake)
get_target_architecture(RESULT_VARIABLE "TARGET_ARCH")

if(CMAKE_GENERATOR_TOOLSET STREQUAL "v141_xp" AND TARGET_ARCH STREQUAL "x86")
    set(ORC_COMPATIBILITY "${TARGET_ARCH}-xp")
else()
    set(ORC_COMPATIBILITY "${TARGET_ARCH}")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(${ORC_ROOT}/cmake/Orc.cmake)
orc_add_compile_options()

# Setup repository related variables and force refresh on new commits
include(${ORC_ROOT}/cmake/GitInfo.cmake)
git_monitor_status()
git_info(
    REPO_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    VERSION_FROM_SEMVER_TAG ORC_VERSION
    BRANCH ORC_BRANCH
    COMMIT ORC_COMMIT
)

find_package(fmt CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)

set(ORC_ICON "${ORC_ROOT}/res/DFIR-Orc.ico" CACHE STRING "Application icon")
configure_file("${ORC_ROOT}/src/Orc/Icon.h.in" "${GENERATED_SRC_DIR}/Icon.h" @ONLY)

set(ORC_BOOTSTRAP "capsule")
configure_file("Version.h.in" "${GENERATED_SRC_DIR}/Version.h" @ONLY)

set(SRC_CAPSULE
    "OrcCapsule.cpp"
    "OrcCapsuleCli.h"
    "OrcCapsuleCli.cpp"
    "Resource.h"
    "Cmd/Architecture.h"
    "Cmd/Architecture.cpp"
    "Cmd/CmdMain/Runner.h"
    "Cmd/CmdMain/Runner.cpp"
    "Cmd/CmdMain/CmdAdd.cpp"
    "Cmd/CmdMain/CmdAdd.h"
    "Cmd/CmdMain/CmdAdd.cpp"
    "Cmd/CmdMain/CmdGet.h"
    "Cmd/CmdMain/CmdGet.cpp"
    "Cmd/CmdMain/CmdRemove.h"
    "Cmd/CmdMain/CmdRemove.cpp"
    "Cmd/CmdMain/CmdInfo.h"
    "Cmd/CmdMain/CmdInfo.cpp"
    "Cmd/CmdMain/CmdRun.h"
    "Cmd/CmdMain/CmdRun.cpp"
    "Cmd/CmdResource.h"
    "Cmd/CmdResource.cpp"
    "Fmt/std_error_code.h"
    "Fmt/std_filesystem.h"
    "Utilities/Compression.h"
    "Utilities/Compression.cpp"
    "Utilities/Crypto.h"
    "Utilities/Crypto.cpp"
    "Utilities/Error.h"
    "Utilities/File.h"
    "Utilities/File.cpp"
    "Utilities/Format.h"
    "Utilities/Format.cpp"
    "Utilities/Log.h"
    "Utilities/Log.cpp"
    "Utilities/Process.h"
    "Utilities/Process.cpp"
    "Utilities/Resource.h"
    "Utilities/Resource.cpp"
    "Utilities/Scope.h"
    "Utilities/Strings.h"
    "Utilities/Strings.cpp"
    "OrcCapsule.rc"
    )

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}"
    PREFIX "Sources"
    FILES ${SRC_CAPSULE}
)

add_executable(OrcCapsule
    ${SRC_CAPSULE}
    "Compatibility.manifest"
)

target_include_directories(OrcCapsule PRIVATE ${CAPSULE_ROOT} ${GENERATED_SRC_DIR} zstd::libzstd_static)

# BEWARE: do not add library that would enable side-loading
target_link_options(OrcCapsule PRIVATE /NODEFAULTLIB)
target_link_libraries(OrcCapsule
    PRIVATE
        advapi32.lib
        kernel32.lib
        user32.lib
        $<IF:$<CONFIG:Debug>,libucrtd.lib,libucrt.lib>
        $<IF:$<CONFIG:Debug>,libcmtd.lib,libcmt.lib>
        $<IF:$<CONFIG:Debug>,libcpmtd.lib,libcpmt.lib>
        $<IF:$<CONFIG:Debug>,libvcruntimed.lib,libvcruntime.lib>
        fmt::fmt
        zstd::libzstd_static
)

target_link_options(OrcCapsule PRIVATE "/MANIFESTUAC:level='requireAdministrator'")

install(TARGETS OrcCapsule
    CONFIGURATIONS Debug MinSizeRel Release RelWithDebInfo
    DESTINATION "$<CONFIG>"
)

install(FILES $<TARGET_PDB_FILE:OrcCapsule>
    CONFIGURATIONS Debug MinSizeRel Release RelWithDebInfo
    DESTINATION "$<CONFIG>"
    OPTIONAL
)

# Post-build: embed the binaries
function(add_orc_binary_command BINARY_PATH ARCH)
    if(NOT ARCH)
        message(FATAL_ERROR "Architecture argument is required.")
    endif()

    if(NOT BINARY_PATH OR NOT EXISTS "${BINARY_PATH}")
        return()
    endif()

    add_custom_command(
        TARGET OrcCapsule
        POST_BUILD
        COMMAND $<TARGET_FILE:OrcCapsule> capsule --set ${ARCH} "${BINARY_PATH}"
        COMMENT "Embedding external ${ARCH} binary from ${BINARY_PATH}"
        VERBATIM
    )
endfunction()

if(ORC_RUN32_PATH)
    add_orc_binary_command("${ORC_RUN32_PATH}" "x86")
endif()

if(ORC_RUN64_PATH)
    add_orc_binary_command("${ORC_RUN64_PATH}" "x64")
endif()

if(ORC_RUN32XP_PATH)
    add_orc_binary_command("${ORC_RUN32XP_PATH}" "x86-xp")
endif()
