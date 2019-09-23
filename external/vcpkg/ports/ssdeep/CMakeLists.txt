cmake_minimum_required(VERSION 3.7)

option(SSDEEP_DISABLE_POSITION_ARRAY
    "Disable bit-parallel string operations." OFF)

option(INSTALL_HEADERS "Install public header files" OFF)

include(CheckIncludeFile)
include(CheckFunctionExists)

check_include_file(dirent.h HAVE_DIRENT_H)
check_include_file(dlfcn.h HAVE_DLFCN_H)
check_include_file(fcntl.h HAVE_FCNTL_H)
check_include_file(inttypes.h HAVE_INTTYPES_H)
check_include_file(libgen.h HAVE_LIBGEN_H)
check_include_file(memory.h HAVE_MEMORY_H)
check_include_file(stdint.h HAVE_STDINT_H)
check_include_file(stdlib.h HAVE_STDLIB_H)
check_include_file(strings.h HAVE_STRINGS_H)
check_include_file(string.h HAVE_STRING_H)
check_include_file(sys/disk.h HAVE_SYS_DISK_H)
check_include_file(sys/ioctl.h HAVE_SYS_IOCTL_H)
check_include_file(sys/mount.h HAVE_SYS_MOUNT_H)
check_include_file(sys/param.h HAVE_SYS_PARAM_H)
check_include_file(sys/stat.h HAVE_SYS_STAT_H)
check_include_file(sys/types.h HAVE_SYS_TYPES_H)
check_include_file(unistd.h HAVE_UNISTD_H)
check_include_file(wchar.h HAVE_WCHAR_H)

check_function_exists(ftello.h HAVE_FSEEKO)

configure_file(config.h.in.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)

set(FUZZY_SOURCES
    edit_dist.h
    edit_dist.c
    find-file-size.c
    fuzzy.h
    fuzzy.c
)

if(MSVC)
    list(APPEND FUZZY_SOURCES "fuzzy.def")
endif()

add_library(fuzzy ${FUZZY_SOURCES})

target_compile_definitions(fuzzy PRIVATE -DHAVE_CONFIG_H)

target_include_directories(fuzzy
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
    INTERFACE
        $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_definitions(fuzzy PRIVATE -D_CRT_SECURE_NO_WARNINGS)
    target_compile_options(fuzzy PRIVATE $<$<CONFIG:RELEASE>:/guard:cf>)
endif()

set(PUBLIC_HEADERS fuzzy.h)

if(INSTALL_HEADERS)
    set_target_properties(fuzzy PROPERTIES PUBLIC_HEADER "${PUBLIC_HEADERS}")
endif()

install(
    TARGETS fuzzy
    EXPORT fuzzy-export
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    PUBLIC_HEADER DESTINATION include/ssdeep
)

install(
    EXPORT fuzzy-export
    DESTINATION share/ssdeep
    FILE ssdeepConfig.cmake
    NAMESPACE ssdeep::
)
