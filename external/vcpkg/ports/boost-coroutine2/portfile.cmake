# Automatically generated by boost-vcpkg-helpers/generate-ports.ps1

include(vcpkg_common_functions)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO boostorg/coroutine2
    REF boost-1.70.0
    SHA512 8f087e33be6eeb1bea523abf871a6bb5252bcb0c40de3489d2713c8b242de97697e86fc2628fe0b0eefefef65307cdd0e0737ae7471fde083a2b09d7139f487c
    HEAD_REF master
)

include(${CURRENT_INSTALLED_DIR}/share/boost-vcpkg-helpers/boost-modular-headers.cmake)
boost_modular_headers(SOURCE_PATH ${SOURCE_PATH})