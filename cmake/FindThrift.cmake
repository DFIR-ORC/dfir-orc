#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

#
# Defines:
#   thrift::thrift
#   ${THRIFT_BINARY_PATH}
#

find_library(THRIFT_LIB_DEBUG NAMES thriftmdd)
find_library(THRIFT_LIB_RELEASE NAMES thriftmd)

add_library(thrift::thrift_debug INTERFACE IMPORTED)
target_link_libraries(thrift::thrift_debug
    INTERFACE
        ${THRIFT_LIB_DEBUG}
)

add_library(thrift::thrift_release INTERFACE IMPORTED)
target_link_libraries(thrift::thrift_release
    INTERFACE
        ${THRIFT_LIB_RELEASE}
)

add_library(thrift::thrift INTERFACE IMPORTED)
target_link_libraries(thrift::thrift
    INTERFACE
        debug ${THRIFT_LIB_DEBUG} optimized ${THRIFT_LIB_RELEASE}
)

find_file(THRIFT_BINARY_PATH
    NAMES thrift.exe
    PATH_SUFFIXES tools/thrift
)
