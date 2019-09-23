#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

find_library(LZ4_LIB_DEBUG NAMES lz4d)
find_library(LZ4_LIB_RELEASE NAMES lz4)

add_library(LZ4::LZ4 INTERFACE IMPORTED)

target_link_libraries(LZ4::LZ4
    INTERFACE
        debug ${LZ4_LIB_DEBUG} optimized ${LZ4_LIB_RELEASE}
)
