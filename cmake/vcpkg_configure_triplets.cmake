# Generate triplets dynamically for convenience

function(vcpkg_configure_triplets)
    # Unfortunately vcpkg does not currently support v141_xp so v141 will be use
    # even if it makes no guarantees that it will be compatible with xp...
    if("${CMAKE_GENERATOR_TOOLSET}" STREQUAL "v141_xp")
        message(WARNING "vcpkg: using v141 toolset as v141_xp is not supported")
        set(VCPKG_PLATFORM_TOOLSET v141)
    else()
        set(VCPKG_PLATFORM_TOOLSET ${CMAKE_GENERATOR_TOOLSET})
    endif()

    set(VCPKG_C_FLAGS "")
    set(VCPKG_CXX_FLAGS "/std:c++17 /D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS")
    set(VCPKG_CMAKE_SYSTEM_VERSION ${CMAKE_SYSTEM_VERSION})


    # static triplets
    set(VCPKG_CRT_LINKAGE static)
    set(VCPKG_LIBRARY_LINKAGE static)

    set(VCPKG_TARGET_ARCHITECTURE x86)
    configure_file(
        ${ORC_ROOT}/cmake/triplet.cmake.in
        ${ORC_VCPKG_ROOT}/triplets/x86-windows-static.cmake
    )

    set(VCPKG_TARGET_ARCHITECTURE x64)
    configure_file(
        ${ORC_ROOT}/cmake/triplet.cmake.in
        ${ORC_VCPKG_ROOT}/triplets/x64-windows-static.cmake
    )


    # shared triplets
    set(VCPKG_CRT_LINKAGE dynamic)
    set(VCPKG_LIBRARY_LINKAGE dynamic)

    set(VCPKG_TARGET_ARCHITECTURE x86)
    configure_file(
        ${ORC_ROOT}/cmake/triplet.cmake.in
        ${ORC_VCPKG_ROOT}/triplets/x86-windows.cmake
    )

    set(VCPKG_TARGET_ARCHITECTURE x64)
    configure_file(
        ${ORC_ROOT}/cmake/triplet.cmake.in
        ${ORC_VCPKG_ROOT}/triplets/x64-windows.cmake
    )
endfunction()
