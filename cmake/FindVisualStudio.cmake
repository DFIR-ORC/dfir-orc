#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

function(remove_lines IN_FILE pattern)
  file(STRINGS "${IN_FILE}" LINES)
  file(WRITE "${IN_FILE}" "")
  foreach(LINE IN LISTS LINES)
    string(REGEX REPLACE "${pattern}" "" STRIPPED "${LINE}")
    file(APPEND "${IN_FILE}" "${STRIPPED}\n")
  endforeach()
endfunction()

if(DEFINED ENV{VSINSTALLDIR})
    set(VS_INSTALL_DIR $ENV{VSINSTALLDIR})
elseif(DEFINED ENV{VCINSTALLDIR})
    set(VS_INSTALL_DIR $ENV{VCINSTALLDIR}/..)
else()
    set(VS_INSTALL_DIR ${CMAKE_GENERATOR_INSTANCE})
endif()
get_filename_component(VS_INSTALL_DIR "${VS_INSTALL_DIR}" ABSOLUTE)

if (NOT ORC_BUILD_TEST)
    return()
endif()

#from architecture.cmake
get_target_architecture(RESULT_VARIABLE "TARGET_ARCH")

## CppUnitTest
find_path(CPPUNITTEST_INCLUDE_DIR "CppUnitTest.h"
    PATHS
        "${VS_INSTALL_DIR}/VC"
        "${VS_INSTALL_DIR}/VC/Auxiliary/VS"
    PATH_SUFFIXES
        "UnitTest/include"
    DOC
        "VS UnitTest include directory"
)

set(LIBRARY_NAME "Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib")

find_path(CPPUNITTEST_LIB_DIR "${LIBRARY_NAME}"
    PATHS
        "${VS_INSTALL_DIR}"
        "${VS_INSTALL_DIR}/VC/Auxiliary/VS"
    PATH_SUFFIXES
        "UnitTest/lib/${TARGET_ARCH}"
    DOC
        "VS UnitTest lib directory"
)

set(CPPUNITTEST_LIBRARY "${CPPUNITTEST_LIB_DIR}/${LIBRARY_NAME}")

## targets
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VisualStudio
    DEFAULT_MSG
    VS_INSTALL_DIR
    CPPUNITTEST_INCLUDE_DIR
)

if(NOT TARGET VisualStudio::CppUnitTest)
    # Copy original header and remove the library inclusion done with '#pragma'
    # because relative path is not supported by clang:
    #
    # lld-link: error: could not open 'x86\Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib': no such file or directory
    #
    if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        set(CPPUNITTEST_INCLUDE_DIR_OVERRIDE "${CMAKE_BINARY_DIR}/VS/UnitTest/")
        file(COPY "${CPPUNITTEST_INCLUDE_DIR}" DESTINATION "${CPPUNITTEST_INCLUDE_DIR_OVERRIDE}")
        remove_lines("${CPPUNITTEST_INCLUDE_DIR_OVERRIDE}/include/CppUnitTestCommon.h" "\#pragma comment\\(lib.*")

        set(CPPUNITTEST_INCLUDE_DIR "${CPPUNITTEST_INCLUDE_DIR_OVERRIDE}/include")
    endif()

    # Instead of including absolute path do the library, the library directory
    # must be include instead because of the following:
    #
    # CppUnitTestCommon.h
    #
    #    if ((defined _M_AMD64) || (defined _AMD64_))
    #        pragma comment(lib, "x64\\Microsoft...CppUnitTestFramework.lib")
    #    elif ((defined _M_ARM) || (defined _ARM_))
    #    ...
    #
    get_filename_component(CPPUNITTEST_LIB_DIR
        "${CPPUNITTEST_LIB_DIR}/.." ABSOLUTE
    )

    add_library(VisualStudio::CppUnitTest INTERFACE IMPORTED)

    target_include_directories(VisualStudio::CppUnitTest
       INTERFACE
           "${CPPUNITTEST_INCLUDE_DIR}"
    )

    target_link_directories(VisualStudio::CppUnitTest
        INTERFACE
            "${CPPUNITTEST_LIB_DIR}"
    )

    target_link_libraries(VisualStudio::CppUnitTest
        INTERFACE
            "${CPPUNITTEST_LIBRARY}"
    )
endif()
