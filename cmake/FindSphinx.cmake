#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

find_program(SPHINX_EXECUTABLE NAMES sphinx-build
    HINTS
    $ENV{SPHINX_DIR}
    PATH_SUFFIXES bin
    DOC "Sphinx documentation generator"
)
 
include(FindPackageHandleStandardArgs)
 
find_package_handle_standard_args(Sphinx DEFAULT_MSG
    SPHINX_EXECUTABLE
)
 
mark_as_advanced(SPHINX_EXECUTABLE)
