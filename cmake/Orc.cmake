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
    /EHa      # Enable C++ exception with SEH (required by Robustness.cpp: _set_se_translator)
  # /Gy       # Enable function level linking
  # /JMC      # Debug only Just My Code
    /Oy-      # Omit frame pointer
   # /Qpar     # Enable Parallel Code Generation
   # /Qspectre-  # No need of mitigation as MS disable theirs when as administrator
    /sdl      # Enable additional security checks
  # /Zi       # Program database for edit and continue (debug only)
    /bigobj
)

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # Multi processor compilation
    add_compile_options("/MP")
else()
    add_compile_options(
        -Wpointer-to-int-cast
        -Wno-deprecated-declarations
        -Wno-defaulted-function-deleted
        -Wno-delete-non-abstract-non-virtual-dtor
        -Wno-enum-compare
        -Wno-final-dtor-non-final-class
        -Wno-inconsistent-missing-override
        -Wno-ignored-pragmas
        -Wno-invalid-noreturn
        -Wno-logical-not-parentheses
        -Wno-microsoft-extra-qualification
        -Wno-microsoft-exception-spec
        -Wno-microsoft-goto
        -Wno-microsoft-include
        -Wno-microsoft-template
        -Wno-missing-braces
        -Wno-nonportable-include-path
        -Wno-overloaded-virtual
        -Wno-parentheses
        -Wno-pessimizing-move
        -Wno-reorder-ctor
        -Wno-return-type
        -Wno-switch
        -Wno-tautological-constant-out-of-range-compare
        -Wno-unknown-pragmas
        -Wno-unknown-warning-option
        -Wno-unused-const-variable
        -Wno-unused-function
        -Wno-unused-lambda-capture
        -Wno-unused-local-typedef
        -Wno-unused-private-field
        -Wno-unused-value
        -Wno-unused-variable
        -Wno-writable-strings
    )

    # TODO: enable SAFESEH when clang add support
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
