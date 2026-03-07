if(EXISTS "${CURRENT_INSTALLED_DIR}/include/openssl/ssl.h")
    message(FATAL_ERROR "Can't build libressl if openssl is installed. Please remove openssl, and try install libressl again if you need it.")
endif()

vcpkg_download_distfile(
    LIBRESSL_SOURCE_ARCHIVE
    URLS "https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/${PORT}-${VERSION}.tar.gz"
         "https://github.com/libressl/portable/releases/download/v${VERSION}/${PORT}-${VERSION}.tar.gz"
    FILENAME "${PORT}-${VERSION}.tar.gz"
    SHA512 988e580b137d9b847288c6a12fc09c4b24113905521aa4e938c964f7845080463e6f2ca3b58d800512ba5c790a06e4e41b31d187ef09c3018f2321b22ecab267
)

vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE "${LIBRESSL_SOURCE_ARCHIVE}"
    PATCHES
        0000-dfirorc-all-in-one.patch
        pkgconfig.diff
)

file(COPY ${CMAKE_CURRENT_LIST_DIR}/inet_pton.c DESTINATION "${SOURCE_PATH}/crypto/compat/")
file(COPY ${CMAKE_CURRENT_LIST_DIR}/bcrypt_gen_random.c DESTINATION "${SOURCE_PATH}/crypto/compat/")

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        "tools" LIBRESSL_APPS
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DLIBRESSL_INSTALL_CMAKEDIR=share/${PORT}
        -DLIBRESSL_TESTS=OFF
    OPTIONS_DEBUG
        -DLIBRESSL_APPS=OFF
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()
vcpkg_cmake_config_fixup()

# libressl as openssl replacement
configure_file("${CURRENT_PORT_DIR}/vcpkg-cmake-wrapper.cmake.in" "${CURRENT_PACKAGES_DIR}/share/openssl/vcpkg-cmake-wrapper.cmake" @ONLY)

# ── Strip ntdll.lib from generated cmake config files ───────────────────────
# ntdll.lib is added by LibreSSL's own CMakeLists for Windows crypto APIs,
# but it is implicitly available on all Windows targets and causes linker
# errors on XP SP2 static builds with older toolchains.
set(_libressl_cmake_configs
    LibreSSL-Crypto.cmake
    LibreSSL-SSL.cmake
    LibreSSL-TLS.cmake
)

foreach(_cmake_config IN LISTS _libressl_cmake_configs)
    set(_cmake_config_path "${CURRENT_PACKAGES_DIR}/share/libressl/${_cmake_config}")
    if(EXISTS "${_cmake_config_path}")
        vcpkg_replace_string("${_cmake_config_path}" "ntdll.lib;" "" IGNORE_UNCHANGED)
        vcpkg_replace_string("${_cmake_config_path}" ";ntdll.lib" "" IGNORE_UNCHANGED)
        vcpkg_replace_string("${_cmake_config_path}" "ntdll.lib"  "" IGNORE_UNCHANGED)
        vcpkg_replace_string("${_cmake_config_path}" ";ntdll;" ";" IGNORE_UNCHANGED)
        vcpkg_replace_string("${_cmake_config_path}" "ntdll;"  ""  IGNORE_UNCHANGED)
        vcpkg_replace_string("${_cmake_config_path}" ";ntdll"  ""  IGNORE_UNCHANGED)
    endif()
endforeach()

# ── Strip ntdll from pkgconfig files ────────────────────────────────────────
set(_libressl_pc_files
    "${CURRENT_PACKAGES_DIR}/lib/pkgconfig/libcrypto.pc"
    "${CURRENT_PACKAGES_DIR}/lib/pkgconfig/libtls.pc"
    "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/libcrypto.pc"
    "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/libtls.pc"
)

foreach(_pc_file IN LISTS _libressl_pc_files)
    if(EXISTS "${_pc_file}")
        vcpkg_replace_string("${_pc_file}" " -lntdll" "" IGNORE_UNCHANGED)
        vcpkg_replace_string("${_pc_file}" "-lntdll"  "" IGNORE_UNCHANGED)
    endif()
endforeach()

if("tools" IN_LIST FEATURES)
    vcpkg_copy_tools(TOOL_NAMES ocspcheck openssl DESTINATION "${CURRENT_PACKAGES_DIR}/tools/openssl" AUTO_CLEAN)
endif()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/etc/ssl/certs"
    "${CURRENT_PACKAGES_DIR}/debug/etc/ssl/certs"
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/share/man"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
