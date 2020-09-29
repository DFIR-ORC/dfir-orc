#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

macro(orc_add_compile_options)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(
    UNICODE
    _UNICODE
    NOMINMAX
    BOOST_NO_SWPRINTF
    _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
    _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
)

if(${TARGET_ARCH} STREQUAL "x64")
    add_compile_definitions(
        _WIN64
    )
endif()

# Fix warning with 'cl' about overriding existing value
string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# TODO: Qspectre disable option is not supported until cmake 3.15.2
add_compile_options(
    /wd4995
    /EHa      # Enable C++ exception with SEH (required by Robustness.cpp: _set_se_translator)
  # /Gy       # Enable function level linking
  # /JMC      # Debug only Just My Code
    /MP       # Multi processor compilation
    /Oy-      # Omit frame pointer
    /Qpar     # Enable Parallel Code Generation
   # /Qspectre-  # No need of mitigation as MS disable theirs when as administrator
    /sdl      # Enable additional security checks
  # /Zi       # Program database for edit and continue (debug only)
    /bigobj
)

# TODO: enable SAFESEH when clang add support
if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_link_options("/SAFESEH:NO")
endif()

list(APPEND COMPILE_OPTIONS_RELEASE
    /guard:cf  # Enable control flow guard
)

foreach(OPTION IN ITEMS ${COMPILE_OPTIONS_DEBUG})
    add_compile_options($<$<CONFIG:DEBUG>:${OPTION}>)
endforeach()

foreach(OPTION IN ITEMS ${COMPILE_OPTIONS_RELEASE})
    add_compile_options($<$<CONFIG:RELEASE>:${OPTION}>)
    add_compile_options($<$<CONFIG:MINSIZEREL>:${OPTION}>)
    add_compile_options($<$<CONFIG:RELWITHDEBINFO>:${OPTION}>)
endforeach()

endmacro()
