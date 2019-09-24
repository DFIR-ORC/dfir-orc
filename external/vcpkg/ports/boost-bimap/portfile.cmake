# Automatically generated by boost-vcpkg-helpers/generate-ports.ps1

include(vcpkg_common_functions)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO boostorg/bimap
    REF boost-1.70.0
    SHA512 9f6938bdbbd32a262cfd4c25b90930df6e0610aacff80a26b7a2caa38fdcaa309bdd7da0405e4529dfaf07314f887eb2b928db89aef1088fdbd7bd1da7bf5c0b
    HEAD_REF master
)

include(${CURRENT_INSTALLED_DIR}/share/boost-vcpkg-helpers/boost-modular-headers.cmake)
boost_modular_headers(SOURCE_PATH ${SOURCE_PATH})