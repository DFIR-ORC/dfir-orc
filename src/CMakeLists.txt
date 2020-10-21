#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

add_subdirectory(OrcLib)

if(ORC_BUILD_COMMAND OR ORC_BUILD_ORC OR ORC_BUILD_FASTFIND)
    add_subdirectory(OrcCommand)
endif()

if(ORC_BUILD_APACHE_ORC)
    add_subdirectory(OrcApacheOrcLib)
    add_subdirectory(OrcApacheOrc)
endif()

# Parquet depends on arrow library which does not exist for x86
if(ORC_BUILD_PARQUET)
    add_subdirectory(OrcParquetLib)
    add_subdirectory(OrcParquet)
endif()

if(ORC_BUILD_ORC)
    add_subdirectory(Orc)
endif()

if(ORC_BUILD_FASTFIND)
    add_subdirectory(FastFind)
endif()
