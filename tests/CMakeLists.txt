#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

add_subdirectory(OrcLibTest)

if(ORC_BUILD_APACHE_ORC)
    add_subdirectory(OrcApacheOrcTest)
endif()

# Parquet depends on arrow library which does not exist for x86
if(ORC_BUILD_PARQUET)
    add_subdirectory(OrcParquetTest)
endif()

if(ORC_BUILD_SQL)
    add_subdirectory(OrcSqlTest)
endif()
