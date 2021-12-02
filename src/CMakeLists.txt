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

    if(ORC_COMMAND_DD)
        add_compile_definitions(ORC_COMMAND_DD)
    endif()

    if(ORC_COMMAND_FASTFIND)
        add_compile_definitions(ORC_COMMAND_FASTFIND)
    endif()

    if(ORC_COMMAND_FATINFO)
        add_compile_definitions(ORC_COMMAND_FATINFO)
    endif()

    if(ORC_COMMAND_GETSAMPLES)
        add_compile_definitions(ORC_COMMAND_GETSAMPLES)
    endif()

    if(ORC_COMMAND_GETSECTORS)
        add_compile_definitions(ORC_COMMAND_GETSECTORS)
    endif()

    if(ORC_COMMAND_GETTHIS)
        add_compile_definitions(ORC_COMMAND_GETTHIS)
    endif()

    if(ORC_COMMAND_NTFSINFO)
        add_compile_definitions(ORC_COMMAND_NTFSINFO)
    endif()

    if(ORC_COMMAND_NTFSUTIL)
        add_compile_definitions(ORC_COMMAND_NTFSUTIL)
    endif()

    if(ORC_COMMAND_OBJINFO)
        add_compile_definitions(ORC_COMMAND_OBJINFO)
    endif()

    if(ORC_COMMAND_REGINFO)
        add_compile_definitions(ORC_COMMAND_REGINFO)
    endif()

    if(ORC_COMMAND_TOOLEMBED)
        add_compile_definitions(ORC_COMMAND_TOOLEMBED)
    endif()

    if(ORC_COMMAND_USNINFO)
        add_compile_definitions(ORC_COMMAND_USNINFO)
    endif()

    if(ORC_COMMAND_WOLFLAUNCHER)
        add_compile_definitions(ORC_COMMAND_WOLFLAUNCHER)
    endif()

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
