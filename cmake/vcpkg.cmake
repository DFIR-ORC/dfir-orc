#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

#
# vcpkg_check_boostrap: check if bootstrap is needed
#
#   ARGUMENTS
#       VCPKG_PATH               path      vcpkg path
#
#   RESULT
#       VCPKG_BOOTSTRAP_FOUND    BOOL      TRUE if vcpkg executable is found
#
function(vcpkg_check_boostrap)
    set(OPTIONS)
    set(SINGLE PATH)
    set(MULTI)

    cmake_parse_arguments(VCPKG "${OPTIONS}" "${SINGLE}" "${MULTI}" ${ARGN})

    set(VCPKG_EXE "${VCPKG_PATH}/vcpkg.exe")
    if(NOT EXISTS "${VCPKG_EXE}")
        execute_process(
            COMMAND "bootstrap-vcpkg.bat"
            WORKING_DIRECTORY ${VCPKG_PATH}
            RESULT_VARIABLE RESULT
        )

        if(NOT "${RESULT}" STREQUAL "0")
            message(FATAL_ERROR
                "Failed to bootstrap using '${VCPKG_PATH}': ${RESULT}"
            )
        endif()
    endif()

    message(STATUS "Using vcpkg: ${VCPKG_EXE}")

    set(VCPKG_BOOTSTRAP_FOUND TRUE PARENT_SCOPE)
endfunction()

#
# vcpkg_install_packages: display and optionally install required dependencies
#
#   ARGUMENTS
#       VCPKG_PATH            path        vcpkg path
#       PACKAGES              list        list of packages to be installed
#       ARCH                  x86/x64     build architecture
#       USE_STATIC_CRT        ON/OFF      use static runtime
#
#   RESULT
#       VCPKG_PACKAGES_FOUND  BOOL        TRUE if pacakges are found
#
function(vcpkg_install_packages)
    set(OPTIONS)
    set(SINGLE PATH ARCH USE_STATIC_CRT)
    set(MULTI PACKAGES OVERLAY_PORTS OVERLAY_TRIPLETS)

    cmake_parse_arguments(VCPKG "${OPTIONS}" "${SINGLE}" "${MULTI}" ${ARGN})

    if(VCPKG_USE_STATIC_CRT)
        set(CRT_LINK "-static")
    endif()

    # Install the package matching the triplet if none provided
    foreach(PKG IN ITEMS ${VCPKG_PACKAGES})
        string(REGEX MATCH ".*:.*" match ${PKG})
        if(match)
            list(APPEND PACKAGES "${PKG}")
        else()
            list(APPEND PACKAGES "${PKG}:${VCPKG_ARCH}-windows${CRT_LINK}")
        endif()
    endforeach()

    list(JOIN PACKAGES " " PACKAGES_STR)

    if(VCPKG_OVERLAY_PORTS)
        foreach(PORT IN ITEMS ${VCPKG_OVERLAY_PORTS})
            string(APPEND OVERLAY_PORTS_STR "--overlay-ports=${PORT} ")
        endforeach()
        string(STRIP ${OVERLAY_PORTS_STR} OVERLAY_PORTS_STR)
    endif()

    if(VCPKG_OVERLAY_TRIPLETS)
        foreach(TRIPLET IN ITEMS ${VCPKG_OVERLAY_TRIPLETS})
            string(APPEND OVERLAY_TRIPLETS_STR "--overlay-triplets=${TRIPLET} ")
        endforeach()
        string(STRIP ${OVERLAY_TRIPLETS_STR} OVERLAY_TRIPLETS_STR)
    endif()

    message(STATUS "Install dependencies with: "
        "\"${VCPKG_PATH}\\vcpkg.exe\""
            " --vcpkg-root \"${VCPKG_PATH}\" "
            "${OVERLAY_PORTS_STR}"
            "${OVERLAY_TRIPLETS_STR}"
            "install ${PACKAGES_STR}\n"
    )

    execute_process(
        COMMAND "vcpkg.exe" --vcpkg-root ${VCPKG_PATH} ${OVERLAY_PORTS_STR} ${OVERLAY_TRIPLETS_STR} install ${PACKAGES}
        WORKING_DIRECTORY ${VCPKG_PATH}
        RESULT_VARIABLE RESULT
    )

    if(NOT "${RESULT}" STREQUAL "0")
        message(FATAL_ERROR "Failed to install packages: ${RESULT}")
    endif()

    set(VCPKG_PACKAGES_FOUND TRUE PARENT_SCOPE)
endfunction()

#
# vcpkg_setup_environment: setup CMAKE_TOOLCHAIN_FILE and VCPKG_TARGET_TRIPLET
#
#   ARGUMENTS
#       VCPKG_PATH            path        vcpkg path
#       ARCH                  x86/x64     build architecture
#       USE_STATIC_CRT        ON/OFF      use static runtime
#
#   RESULT
#       CMAKE_TOOLCHAIN_FILE  path        vcpkg toolchain
#       VCPKG_TARGET_TRIPLET  triplet     triplet to use
#
function(vcpkg_setup_environment)
    set(OPTIONS)
    set(SINGLE PATH ARCH USE_STATIC_CRT)
    set(MULTI PACKAGES)

    cmake_parse_arguments(VCPKG "${OPTIONS}" "${SINGLE}" "${MULTI}" ${ARGN})

    # Deduce CMAKE_TOOLCHAIN_FILE from VCPKG_PATH
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        set(CMAKE_TOOLCHAIN_FILE
            ${VCPKG_PATH}\\scripts\\buildsystems\\vcpkg.cmake
            CACHE
            STRING
            "CMake toolchain file"
            FORCE)
    endif()

    # Define the vcpkg triplet from the generator if not defined
    if(NOT VCPKG_TARGET_TRIPLET)
        if(VCPKG_USE_STATIC_CRT)
            set(CRT_LINK "-static")
        endif()

        set(VCPKG_TARGET_TRIPLET
            "${VCPKG_ARCH}-windows${CRT_LINK}"
            CACHE
            STRING
            "VCPKG target triplet"
            FORCE)
    endif()
endfunction()

#
# vcpkg_install: display and optionally install required dependencies
#
#   ARGUMENTS
#       VCPKG_PATH            path        vcpkg path
#       PACKAGES              list        list of packages to be installed
#       ARCH                  x86/x64     build architecture
#       USE_STATIC_CRT        ON/OFF      use static runtime
#       OVERLAY_PORTS         path        list of overlay directories
#       NO_UPGRADE            <option>    do not upgrade
#
#   RESULT
#       VCPKG_FOUND           BOOL        TRUE if vcpkg is found and setup
#       CMAKE_TOOLCHAIN_FILE  path        vcpkg toolchain
#       VCPKG_TARGET_TRIPLET  triplet     triplet to use
#
function(vcpkg_install)
    set(OPTIONS NO_UPGRADE)
    set(SINGLE PATH ARCH USE_STATIC_CRT)
    set(MULTI PACKAGES OVERLAY_PORTS OVERLAY_TRIPLETS)

    cmake_parse_arguments(VCPKG "${OPTIONS}" "${SINGLE}" "${MULTI}" ${ARGN})

    vcpkg_check_boostrap(
        PATH ${VCPKG_PATH}
    )

    if(NOT VCPKG_NO_UPGRADE)
        vcpkg_upgrade(
            PATH ${VCPKG_PATH}
            OVERLAY_PORTS ${VCPKG_OVERLAY_PORTS}
            OVERLAY_TRIPLETS ${VCPKG_OVERLAY_TRIPLETS}
        )
    endif()

    vcpkg_install_packages(
       PATH ${VCPKG_PATH}
       OVERLAY_PORTS ${VCPKG_OVERLAY_PORTS}
       OVERLAY_TRIPLETS ${VCPKG_OVERLAY_TRIPLETS}
       PACKAGES ${VCPKG_PACKAGES}
       ARCH ${VCPKG_ARCH}
       USE_STATIC_CRT ${VCPKG_USE_STATIC_CRT}
    )

    vcpkg_setup_environment(
        PATH ${VCPKG_PATH}
        ARCH ${VCPKG_ARCH}
        USE_STATIC_CRT ${VCPKG_USE_STATIC_CRT}
    )

    set(VCPKG_FOUND TRUE PARENT_SCOPE)
    message(STATUS "Using toolchain: " ${CMAKE_TOOLCHAIN_FILE})
    message(STATUS "Using vcpkg triplet: " ${VCPKG_TARGET_TRIPLET})
endfunction()


#
# vcpkg_upgrade: upgrade dependencies
#
#   ARGUMENTS
#       VCPKG_PATH            path        vcpkg path
#
function(vcpkg_upgrade)
    set(OPTIONS)
    set(SINGLE PATH)
    set(MULTI OVERLAY_PORTS OVERLAY_TRIPLETS)

    cmake_parse_arguments(VCPKG "${OPTIONS}" "${SINGLE}" "${MULTI}" ${ARGN})

    if(NOT EXISTS ${VCPKG_PATH}/vcpkg.exe)
        return()
    endif()

    # Probe for "vcpkg.exe update available" message
    execute_process(
        COMMAND "vcpkg.exe" --vcpkg-root ${VCPKG_PATH} install 7zip --dry-run
        WORKING_DIRECTORY ${VCPKG_PATH}
        OUTPUT_VARIABLE OUTPUT
        ERROR_QUIET
    )

    string(FIND ${OUTPUT} "Use .\\bootstrap-vcpkg.bat to update." FIND_RESULT)
    if(${FIND_RESULT} GREATER_EQUAL 0)
        message(STATUS "vcpkg update is available")

        execute_process(
            COMMAND "bootstrap-vcpkg.bat"
            WORKING_DIRECTORY ${VCPKG_PATH}
            RESULT_VARIABLE RESULT
        )

        if(${RESULT} LESS 0)
            message(FATAL_ERROR "Failed to upgrade bootstrap: ${RESULT}")
        endif()
    endif()

    if(VCPKG_OVERLAY_PORTS)
        foreach(PORT IN ITEMS ${VCPKG_OVERLAY_PORTS})
            string(APPEND OVERLAY_PORTS_STR "--overlay-ports=${PORT} ")
        endforeach()
        string(STRIP ${OVERLAY_PORTS_STR} OVERLAY_PORTS_STR)
    endif()

    if(VCPKG_OVERLAY_TRIPLETS)
        foreach(TRIPLET IN ITEMS ${VCPKG_OVERLAY_TRIPLETS})
            string(APPEND OVERLAY_TRIPLETS_STR "--overlay-triplets=${TRIPLET} ")
        endforeach()
        string(STRIP ${OVERLAY_TRIPLETS_STR} OVERLAY_TRIPLETS_STR)
    endif()

    execute_process(
        COMMAND "vcpkg.exe" --vcpkg-root ${VCPKG_PATH} ${OVERLAY_PORTS_STR} ${OVERLAY_TRIPLETS_STR} upgrade --no-dry-run
        WORKING_DIRECTORY ${VCPKG_PATH}
        RESULT_VARIABLE RESULT
    )

    if(${RESULT} LESS 0)
        message(FATAL_ERROR "Failed to upgrade vcpkg: ${RESULT}")
    endif()
endfunction()
