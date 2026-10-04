#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2026 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#

include(${ORC_ROOT}/cmake/Orc.cmake)
orc_add_compile_options()

# libFuzzer and AddressSanitizer come with MSVC 17 and later for x64 only, which is why this target is behind
# ORC_BUILD_LZX_FUZZ (off by default: the v141_xp toolset cannot build it).
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "OrcLzxFuzz needs a 64 bit target")
endif()

# AddressSanitizer refuses the run time checks the Debug configuration adds; strip them for this directory only.
string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

add_executable(OrcLzxFuzz "lzx_fuzz.cpp")

target_link_libraries(OrcLzxFuzz PRIVATE OrcLib)

# With ORC_BUILD_TEST on, OrcLib carries UnitTestHelper.cpp, whose CppUnitTest.h pulls the framework library in
# through a '#pragma comment(lib)' with a relative path: the consumer must supply the library directory.
if(ORC_BUILD_TEST)
    find_package(VisualStudio REQUIRED)
    target_link_libraries(OrcLzxFuzz PRIVATE VisualStudio::CppUnitTest)
endif()

target_compile_options(OrcLzxFuzz PRIVATE /fsanitize=address /fsanitize=fuzzer)
target_link_options(OrcLzxFuzz PRIVATE /INCREMENTAL:NO)

# OrcLib is built without AddressSanitizer, so the STL's container annotations must stay off here too or the
# linker refuses the mix (LNK2038 annotate_string / annotate_vector / annotate_optional).
target_compile_definitions(OrcLzxFuzz PRIVATE
    _DISABLE_STRING_ANNOTATION
    _DISABLE_VECTOR_ANNOTATION
    _DISABLE_OPTIONAL_ANNOTATION
)

set_target_properties(OrcLzxFuzz PROPERTIES FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}")
