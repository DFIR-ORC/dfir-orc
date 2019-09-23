#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

find_library(BOOST_REGEX_DEBUG_LIB NAMES boost_regex-vc140-mt-gd)
find_library(BOOST_REGEX_RELEASE_LIB NAMES boost_regex-vc140-mt)

add_library(boost::regex INTERFACE IMPORTED)

target_link_libraries(boost::regex
    INTERFACE
        debug ${BOOST_REGEX_DEBUG_LIB} optimized ${BOOST_REGEX_RELEASE_LIB}
)
