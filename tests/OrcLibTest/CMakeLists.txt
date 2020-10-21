#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#            Jean Gautier
#

include(${ORC_ROOT}/cmake/Orc.cmake)
orc_add_compile_options()

find_package(Boost REQUIRED)
find_package(VisualStudio REQUIRED)

set(SRC_COMMON
    "targetver.h"
    "resource.h"
    "OrcLibTest.cpp"
)

source_group(Common FILES ${SRC_COMMON})

set(SRC_DISK_VOLUME
    "DiskExtentTest.h"
    "VolumeReaderTest.h"
    "DiskExtentTest.cpp"
    "disk_extent_test.cpp"
    "VolumeReaderTest.cpp"
)

source_group(Disk\\Volume FILES ${SRC_DISK_VOLUME})

set(SRC_DISK_FS_NTFS_MFT
    "mft_reccord_test.cpp"
    "mft_walker_test.cpp"
)

source_group(Disk\\FS\\NTFS\\MFT FILES ${SRC_DISK_FS_NTFS_MFT})

set(SRC_DISK_FS_NTFS_USN
    "usn_journal_test.cpp"
    "usn_walker_test.cpp"
)

source_group(Disk\\FS\\NTFS\\USN FILES ${SRC_DISK_FS_NTFS_USN})

set(SRC_UTILITIES
    "binary_buffer_test.cpp"
    "convert.cpp"
    "crypto_utilities_test.cpp"
	"embedded_resource.cpp"
    "exceptions.cpp"
    "libraries_test.cpp"
    "profile_list.cpp"
    "registry.cpp"
    "temporary.cpp"
    "result.cpp"
    "system_details.cpp"
    "wide_ansi.cpp"
)

source_group(Utilities FILES ${SRC_UTILITIES})

set(SRC_DISK
    "partition_table_test.cpp"
    "partition_test.cpp"
    "reparse_point.cpp"
    "wof.cpp"
)

source_group(Disk FILES ${SRC_DISK})

set(SRC_INOUT_BYTESTREAM_FSSTREAM "fat_stream_test.cpp")
source_group(InOut\\ByteStream\\FSStream FILES ${SRC_INOUT_BYTESTREAM_FSSTREAM})

set(SRC_DISK_FS_FAT
    "fat_file_entry_test.cpp"
    "fat_table_test.cpp"
    "fat_walker_test.cpp"
)

source_group(Disk\\FS\\Fat FILES ${SRC_DISK_FS_FAT})

set(SRC_INOUT_BYTESTREAM_CRYPTOSTREAM
    "hash_stream_test.cpp"
    "fuzzy_hash_stream.cpp"
)

source_group(InOut\\ByteStream\\CryptoStream
    FILES
        ${SRC_INOUT_BYTESTREAM_CRYPTOSTREAM}
)

set(SRC_INOUT_BYTESTREAM "bufferstream.cpp")
source_group(InOut\\ByteStream FILES ${SRC_INOUT_BYTESTREAM})

set(SRC_INOUT_STRUCTUREDOUTPUT "structured_output_test.cpp")
source_group(InOut\\StructuredOutput FILES ${SRC_INOUT_STRUCTUREDOUTPUT})

set(SRC_RUNNINGCODE "running_code_test.cpp")
source_group(RunningCode FILES ${SRC_RUNNINGCODE})

set(SRC_AUTHENTICODE "authenticode_test.cpp")
source_group(Authenticode FILES ${SRC_AUTHENTICODE})

set(SRC_LOCATIONS "locations.cpp")
source_group(Locations FILES ${SRC_LOCATIONS})

set(SRC_YARA "yara_basic.cpp" "yara_scanner.cpp")
source_group(Yara FILES ${SRC_YARA})

set(SRC_INOUT_TABLEOUTPUT "table_output.cpp")
source_group(InOut\\TableOutput FILES ${SRC_INOUT_TABLEOUTPUT})

set(SRC_SUPPORTINGTESTFILES "buffer.cpp")
source_group(SupportingTestFiles FILES ${SRC_SUPPORTINGTESTFILES})

set(SRC_SUPPORTINGTESTFILES_NTFS "ntfs_images/ntfs.7z")
source_group(SupportingTestFiles\\NTFS FILES ${SRC_SUPPORTINGTESTFILES_NTFS})

set(SRC_SUPPORTINGTESTFILES_FAT
    "fat_images/FAT32.7z"
    "fat_images/Fat12.7z"
    "fat_images/Fat16.7z"
)

source_group(SupportingTestFiles\\FAT FILES ${SRC_SUPPORTINGTESTFILES_FAT})

set(SRC_SUPPORTINGTESTFILES_USN
    "usn_journal/winxp.7z"
    "usn_journal/win7.7z"
    "usn_journal/win10.7z"
)

source_group(SupportingTestFiles\\USN FILES ${SRC_SUPPORTINGTESTFILES_USN})

set(SRC_PLAYLISTS
    "Disk.playlist"
    "FAT.playlist"
    "NTFS.playlist"
    "RunningCode.playlist"
    "Streams.playlist"
    "Utilities.playlist"
)

source_group(Playlists FILES ${SRC_PLAYLISTS})

add_library(OrcLibTest
    SHARED
        "stdafx.h"
        "stdafx.cpp"
		"OrcLibTest.rc"
        ${SRC_COMMON}
        ${SRC_DISK_VOLUME}
        ${SRC_DISK_FS_NTFS_MFT}
        ${SRC_DISK_FS_NTFS_USN}
        ${SRC_UTILITIES}
        ${SRC_DISK}
        ${SRC_DISK_FS_FAT}
        ${SRC_INOUT_BYTESTREAM}
        ${SRC_INOUT_BYTESTREAM_FSSTREAM}
        ${SRC_INOUT_BYTESTREAM_CRYPTOSTREAM}
        ${SRC_INOUT_STRUCTUREDOUTPUT}
        ${SRC_RUNNINGCODE}
        ${SRC_AUTHENTICODE}
        ${SRC_LOCATIONS}
        ${SRC_YARA}
        ${SRC_INOUT_TABLEOUTPUT}
        ${SRC_PLAYLISTS}
        ${SRC_SUPPORTINGTESTFILES}
        ${SRC_SUPPORTINGTESTFILES_NTFS}
        ${SRC_SUPPORTINGTESTFILES_FAT}
        ${SRC_SUPPORTINGTESTFILES_USN}
        ${SRC_SUPPORTINGTESTFILES_DIA}
    )

target_include_directories(OrcLibTest PRIVATE ${Boost_INCLUDE_DIRS})

target_link_libraries(OrcLibTest
    PRIVATE
        VisualStudio::atls
        OrcLib
)

target_precompile_headers(OrcLibTest PRIVATE stdafx.h)

set_target_properties(OrcLibTest PROPERTIES FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}")

# Embed the dll in itself for "SimpleValue" test
add_custom_command(
    TARGET OrcLibTest
    POST_BUILD
    DEPENDS rcedit

    COMMAND $<TARGET_FILE:rcedit>
        set --type "VALUES"
            --name "TEST_7Z_DLL"
            --value-utf16 "7z:#TEST_7Z_DLL_BIN^|OrcLibTest.dll"
            $<TARGET_FILE:OrcLibTest>

    COMMAND $<TARGET_FILE:rcedit>
        set --type "BINARY"
            --name "TEST_7Z_DLL_BIN"
            --value-path $<TARGET_FILE:OrcLibTest>
            --compress=7z
            $<TARGET_FILE:OrcLibTest>
)
