# Generate triplets dynamically for convenience

function(vcpkg_configure_triplets)
    set(OPTIONS)
    set(SINGLE OUTPUT_TRIPLETS_DIR)
    set(MULTI)

    cmake_parse_arguments(ARG "${OPTIONS}" "${SINGLE}" "${MULTI}" ${ARGN})

    file(MAKE_DIRECTORY ${ARG_OUTPUT_TRIPLETS_DIR})

    if("${CMAKE_SYSTEM_VERSION}" STREQUAL "5.1")
        # Target XP SP2 with /DNTDDI_VERSION=0x05010200
        set(WINDOWS_VERSION_FLAGS "/D_WIN32_WINNT=0x0501 /DWINVER=0x0501 /DNTDDI_VERSION=0x05010200")
    elseif("${CMAKE_SYSTEM_VERSION}" STREQUAL "6.1")
        set(WINDOWS_VERSION_FLAGS "${VCPKG_CXX_FLAGS} /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /DNTDDI_VERSION=0x06010000")
    endif()

    # Unfortunately vcpkg does not currently support v141_xp so v141 will be use
    # even if it makes no guarantees that it will be compatible with xp...
    if("${CMAKE_GENERATOR_TOOLSET}" STREQUAL "v141_xp")
        message(WARNING "vcpkg: using v141 toolset as v141_xp is not supported")
        set(VCPKG_PLATFORM_TOOLSET v141)
    else()
        set(VCPKG_PLATFORM_TOOLSET ${CMAKE_GENERATOR_TOOLSET})
    endif()

    set(VCPKG_C_FLAGS "${WINDOWS_VERSION_FLAGS}")
    set(VCPKG_CXX_FLAGS "/std:c++17 /D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS ${WINDOWS_VERSION_FLAGS}")
    set(VCPKG_CMAKE_SYSTEM_VERSION ${CMAKE_SYSTEM_VERSION})

    # static triplets
    set(VCPKG_CRT_LINKAGE static)
    set(VCPKG_LIBRARY_LINKAGE static)

    set(VCPKG_TARGET_ARCHITECTURE x86)
    configure_file(
        ${ORC_ROOT}/cmake/triplet.cmake.in
        ${ARG_OUTPUT_TRIPLETS_DIR}/x86-windows-static.cmake
    )

    set(VCPKG_TARGET_ARCHITECTURE x64)
    configure_file(
        ${ORC_ROOT}/cmake/triplet.cmake.in
        ${ARG_OUTPUT_TRIPLETS_DIR}/x64-windows-static.cmake
    )


    # shared triplets
    set(VCPKG_CRT_LINKAGE dynamic)
    set(VCPKG_LIBRARY_LINKAGE dynamic)

    set(VCPKG_TARGET_ARCHITECTURE x86)
    configure_file(
        ${ORC_ROOT}/cmake/triplet.cmake.in
        ${ARG_OUTPUT_TRIPLETS_DIR}/x86-windows.cmake
    )

    set(VCPKG_TARGET_ARCHITECTURE x64)
    configure_file(
        ${ORC_ROOT}/cmake/triplet.cmake.in
        ${ARG_OUTPUT_TRIPLETS_DIR}/x64-windows.cmake
    )
endfunction()
