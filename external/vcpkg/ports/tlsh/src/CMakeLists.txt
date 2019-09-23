# TLSH is provided for use under two licenses: Apache OR BSD.
# Users may opt to use either license depending on the license
# restictions of the systems with which they plan to integrate
# the TLSH code.
#
# ==============
# Apache License
# ==============
# Copyright 2013 Trend Micro Incorporated
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# ===========
# BSD License
# ===========
# Copyright (c) 2013, Trend Micro Incorporated
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.

# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software without
#    specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
# OF THE POSSIBILITY OF SUCH DAMAGE.

set(TLSH_SRCS
    tlsh.cpp
    tlsh_impl.cpp
    tlsh_util.cpp
)

add_library(tlsh ${TLSH_SRCS})

target_include_directories(tlsh
    PUBLIC
        $<BUILD_INTERFACE:${TLSH_BINARY_DIR}/include>
        $<BUILD_INTERFACE:${TLSH_SOURCE_DIR}/include>
    INTERFACE
        $<INSTALL_INTERFACE:include>
)

target_compile_definitions(tlsh
    PUBLIC
        -D${TLSH_BUCKETS_SIZE}
        -D${TLSH_CHECKSUM_TYPE}
        -D$<$<CXX_COMPILER_ID:MSVC>:WINDOWS>
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(tlsh PRIVATE -DTLSH_EXPORTS)

    set_target_properties(tlsh
        PROPERTIES
            VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}"
            SOVERSION "0"
    )
else()
    target_compile_definitions(tlsh PUBLIC -DTLSH_LIB)
endif()

target_link_libraries(tlsh PRIVATE $<$<CXX_COMPILER_ID:MSVC>:winfunc>)

if(NOT TLSH_NO_INSTALL_HEADERS)
    set(PUBLIC_HEADERS
        "${TLSH_SOURCE_DIR}/include/tlsh.h"
        "${TLSH_BINARY_DIR}/include/version.h"
        )
    set_target_properties(tlsh PROPERTIES PUBLIC_HEADER "${PUBLIC_HEADERS}")
endif()

install(
    TARGETS tlsh
    EXPORT tlsh-export
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/tlsh
)

install(
    EXPORT tlsh-export
    DESTINATION share/tlsh
    FILE tlshConfig.cmake
    NAMESPACE tlsh::
)
