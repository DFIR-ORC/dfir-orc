#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

#
# get_ARCHitecture: deduce target ARCH from generator or
# environment variables
#
#   ARGUMENTS
#       RESULT_VARIABLE       STRING      Return value name for "x86" or "x64"
#
function(get_target_architecture)
    set(OPTIONS)
    set(SINGLE RESULT_VARIABLE)
    set(MULTI)

    cmake_parse_arguments(ARCH "${OPTIONS}" "${SINGLE}" "${MULTI}" ${ARGN})

    # CMAKE_GENERATOR_PLATFORM is only defined from vs2019 generator
    if(CMAKE_GENERATOR_PLATFORM)
        if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
            set(ARCH "x86")
        else()
            set(ARCH ${CMAKE_GENERATOR_PLATFORM})
        endif()

    # Environment variables are defined when cmake is run from VS command prompt
    elseif(DEFINED ENV{VSCMD_ARG_TGT_ARCH})
        set(ARCH $ENV{VSCMD_ARG_TGT_ARCH})
    
    elseif(CMAKE_GENERATOR MATCHES "^Visual Studio [0-9]+ [0-9]+ Win64$")
        set(ARCH "x64")
    
    elseif(CMAKE_GENERATOR MATCHES "^Visual Studio [0-9]+ ([0-9]+)$")
        # Workaround for CMake not detecting the correct toolchain in this case
        if(${CMAKE_MATCH_1} STREQUAL "2019" AND NOT CMAKE_VERSION MATCHES "MSVC.*")
            message(FATAL_ERROR "Please enforce x86 ARCH with \"-A Win32\"")
        endif()
        
        set(ARCH "x86")

    elseif(CMAKE_GENERATOR MATCHES "Ninja")
        set(ARCH "x64")

    else()
        message(FATAL_ERROR "Unknown or unsupported generator")
    endif()

    set(${ARCH_RESULT_VARIABLE} ${ARCH} PARENT_SCOPE)
endfunction()
