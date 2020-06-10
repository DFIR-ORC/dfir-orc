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

find_package(7zip CONFIG REQUIRED)
find_package(boost REQUIRED)
find_package(fmt REQUIRED)
find_package(tlsh CONFIG REQUIRED)
find_package(VisualStudio REQUIRED)
find_package(Yara REQUIRED)



set(SRC_COMMAND
    "CommandAgent.cpp"
    "CommandAgent.h"
    "CommandAgentResources.cpp"
    "CommandAgentResources.h"
    "CommandExecute.cpp"
    "CommandExecute.h"
    "CommandMessage.cpp"
    "CommandMessage.h"
    "CommandNotification.cpp"
    "CommandNotification.h"
    "DbgHelpLibrary.cpp"
    "DbgHelpLibrary.h"
    "DebugAgent.cpp"
    "DebugAgent.h"
    "ProcessRedirect.cpp"
    "ProcessRedirect.h"
)

source_group(Command FILES ${SRC_COMMAND})

set(SRC_COMMON
    "OrcLib.h"
    "targetver.h"
)

# stdafx.[cpp|h] must be included first, don't include them in SRC_COMMON
source_group(Common FILES "stdafx.h" "stdafx.cpp" ${SRC_COMMON})

set(SRC_CONFIG
    "ConfigFile.cpp"
    "ConfigFile.h"
    "ConfigFile_Common.cpp"
    "ConfigFile_Common.h"
    "ConfigFileReader.cpp"
    "ConfigFileReader.h"
    "ConfigFileWriter.cpp"
    "ConfigFileWriter.h"
    "ConfigItem.cpp"
    "ConfigItem.h"
)

source_group(Config FILES ${SRC_CONFIG})

set(SRC_DISK
    "DiskStructures.h"
    "Partition.cpp"
    "Partition.h"
    "PartitionTable.cpp"
    "PartitionTable.h"
)

source_group(Disk FILES ${SRC_DISK})

set(SRC_DISK_FILESYSTEM
    "DataDetails.h"
    "FileInfo.cpp"
    "FileInfo.h"
    "FSUtils.h"
    "FSVBR.h"
    "GenDataStructure.h"
    "BitLocker.h"
    "PEInfo.cpp"
    "PEInfo.h"
)

source_group(Disk\\FileSystem FILES ${SRC_DISK_FILESYSTEM})

set(SRC_DISK_FILESYSTEM_FAT
    "FileSystem.h"
    "FatDataStructures.h"
    "FatFileEntry.cpp"
    "FatFileEntry.h"
    "FatFileInfo.cpp"
    "FatFileInfo.h"
    "FatFileInfo_ColumnDef.cpp"
    "FatTable.h"
    "FatTableEntry.h"
    "FatWalker.cpp"
    "FatWalker.h"
    "SegmentDetails.h"
)

source_group(Disk\\FileSystem\\FAT FILES ${SRC_DISK_FILESYSTEM_FAT})

set(SRC_DISK_FILESYSTEM_NTFS
    "FileFind.cpp"
    "FileFind.h"
    "NTFSCompression.cpp"
    "NTFSCompression.h"
    "NtfsDataStructures.h"
)

source_group(Disk\\FileSystem\\NTFS FILES ${SRC_DISK_FILESYSTEM_NTFS})

set(SRC_DISK_FILESYSTEM_NTFS_FILEINFO
    "MFTRecordFileInfo.cpp"
    "MFTRecordFileInfo.h"
    "NtfsFileInfo.cpp"
    "NtfsFileInfo.h"
    "NtfsFileInfo_ColumnDef.cpp"
    "USNRecordFileInfo.cpp"
    "USNRecordFileInfo.h"
)

source_group(Disk\\FileSystem\\NTFS\\FileInfo
    FILES ${SRC_DISK_FILESYSTEM_NTFS_FILEINFO}
)

set(SRC_DISK_FILESYSTEM_NTFS_MFT
    "IMFT.h"
    "MFTOffline.cpp"
    "MFTOffline.h"
    "MFTOnline.cpp"
    "MFTOnline.h"
    "MFTUtils.cpp"
    "MFTUtils.h"
    "MFTWalker.cpp"
    "MFTWalker.h"
)

source_group(Disk\\FileSystem\\NTFS\\MFT FILES ${SRC_DISK_FILESYSTEM_NTFS_MFT})

set(SRC_DISK_FILESYSTEM_NTFS_MFT_RECORD
    "AttributeList.cpp"
    "AttributeList.h"
    "MFTRecord.cpp"
    "MFTRecord.h"
    "MftRecordAttribute.cpp"
    "MftRecordAttribute.h"
)

source_group(Disk\\FileSystem\\NTFS\\MFT\\Record
    FILES ${SRC_DISK_FILESYSTEM_NTFS_MFT_RECORD}
)

set(SRC_DISK_FILESYSTEM_NTFS_MFT_USN
    "IUSNJournalWalker.h"
    "USNJournalWalker.cpp"
    "USNJournalWalker.h"
    "USNJournalWalkerBase.cpp"
    "USNJournalWalkerBase.h"
    "USNJournalWalkerOffline.cpp"
    "USNJournalWalkerOffline.h"
    )

source_group(Disk\\FileSystem\\NTFS\\MFT\\USN
    FILES ${SRC_DISK_FILESYSTEM_NTFS_MFT_USN}
)

set(SRC_DISK_FILESYSTEM_REFS RefsDataStructures.h)

source_group(Disk\\FileSystem\\ReFS FILES ${SRC_DISK_FILESYSTEM_REFS})

set(SRC_DISK_LOCATION
    "Location.cpp"
    "Location.h"
    "LocationSet.cpp"
    "LocationSet.h"
)

source_group(Disk\\Location FILES ${SRC_DISK_LOCATION})

set(SRC_DISK_VOLUME
    "CompleteVolumeReader.cpp"
    "CompleteVolumeReader.h"
    "DiskExtent.cpp"
    "DiskExtent.h"
    "EnumDisk.cpp"
    "EnumDisk.h"
    "IDiskExtent.h"
    "ImageReader.cpp"
    "ImageReader.h"
    "InterfaceReader.cpp"
    "InterfaceReader.h"
    "MountedVolumeReader.cpp"
    "MountedVolumeReader.h"
    "OfflineMFTReader.cpp"
    "OfflineMFTReader.h"
    "PhysicalDiskReader.cpp"
    "PhysicalDiskReader.h"
    "SnapshotVolumeReader.cpp"
    "SnapshotVolumeReader.h"
    "SystemStorageReader.cpp"
    "SystemStorageReader.h"
    "VHDVolumeReader.cpp"
    "VHDVolumeReader.h"
    "VolumeReader.cpp"
    "VolumeReader.h"
    "VolumeShadowCopies.cpp"
    "VolumeShadowCopies.h"
    "VssAPIExtension.cpp"
    "VssAPIExtension.h"
)

source_group(Disk\\Volume FILES ${SRC_DISK_VOLUME})

set(SRC_EXTENSIONLIBRARIES
    "XmlLiteExtension.cpp"
    "XmlLiteExtension.h"
    "COMExtension.cpp"
    "COMExtension.h"
    "CompressAPIExtension.cpp"
    "CompressAPIExtension.h"
    "EvtLibrary.cpp"
    "EvtLibrary.h"
    "ExtensionLibrary.cpp"
    "ExtensionLibrary.h"
    "Kernel32Extension.cpp"
    "Kernel32Extension.h"
    "NtDllExtension.cpp"
    "NtDllExtension.h"
)

source_group(ExtensionLibraries FILES ${SRC_EXTENSIONLIBRARIES})

if(ORC_BUILD_CHAKRACORE)
    set(SRC_EXTENSIONLIBRARIES_CHAKRA
        "ChakraBridge.h"
        "ChakraExtension.cpp"
        "ChakraExtension.h"
        "ChakraMacros.h"
    )

    source_group(ExtensionLibraries\\Chakra FILES ${SRC_EXTENSIONLIBRARIES_CHAKRA})
endif()

set(SRC_EXTENSIONLIBRARIES_YARA
    "YaraStaticExtension.cpp"
    "YaraStaticExtension.h"
    "YaraScanner.cpp"
    "YaraScanner.h"
)

source_group(ExtensionLibraries\\Yara FILES ${SRC_EXTENSIONLIBRARIES_YARA})

set(SRC_INOUT
    "LocationOutput.h"
    "OutputWriter.cpp"
    "OutputWriter.h"
)

source_group(In&Out FILES ${SRC_INOUT})

set(SRC_INOUT_ARCHIVE
    "Archive.cpp"
    "Archive.h"
    "ArchiveAgent.cpp"
    "ArchiveAgent.h"
    "ArchiveCreate.cpp"
    "ArchiveCreate.h"
    "ArchiveExtract.cpp"
    "ArchiveExtract.h"
    "ArchiveFormat.h"
    "ArchiveMessage.cpp"
    "ArchiveMessage.h"
    "ArchiveNotification.cpp"
    "ArchiveNotification.h"
)

source_group(In&Out\\Archive FILES ${SRC_INOUT_ARCHIVE})

set(SRC_INOUT_ARCHIVE_CABINET
    "CabCreate.cpp"
    "CabCreate.h"
    "CabExtract.cpp"
    "CabExtract.h"
)

source_group(In&Out\\Archive\\Cabinet FILES ${SRC_INOUT_ARCHIVE_CABINET})

set(SRC_INOUT_ARCHIVE_SEVENZIP
    "ArchiveExtractCallback.cpp"
    "ArchiveExtractCallback.h"
    "ArchiveOpenCallback.cpp"
    "ArchiveOpenCallback.h"
    "ArchiveUpdateCallback.cpp"
    "ArchiveUpdateCallback.h"
    "GUIDs.cpp"
    "PropVariant.cpp"
    "PropVariant.h"
    "ZipCreate.cpp"
    "ZipCreate.h"
    "ZipExtract.cpp"
    "ZipExtract.h"
    "ZipLibrary.cpp"
    "ZipLibrary.h"
)

source_group(In&Out\\Archive\\SevenZip FILES ${SRC_INOUT_ARCHIVE_SEVENZIP})

set(SRC_INOUT_BYTESTREAM
    "ByteStream.cpp"
    "ByteStream.h"
)

source_group(In&Out\\ByteStream FILES ${SRC_INOUT_BYTESTREAM})

set(SRC_INOUT_BYTESTREAM_CRYPTOSTREAM
    "CryptoHashStream.cpp"
    "CryptoHashStream.h"
    "FuzzyHashStream.cpp"
    "FuzzyHashStream.h"
    "HashStream.cpp"
    "HashStream.h"
    "PasswordEncryptedStream.cpp"
    "PasswordEncryptedStream.h"
    "XORStream.cpp"
    "XORStream.h"
)

source_group(In&Out\\ByteStream\\CryptoStream
    FILES ${SRC_INOUT_BYTESTREAM_CRYPTOSTREAM}
)

set(SRC_INOUT_BYTESTREAM_FSSTREAM_FATSTREAM
    "FatStream.cpp"
    "FatStream.h"
)

source_group(In&Out\\ByteStream\\FSStream\\FatStream
    FILES ${SRC_INOUT_BYTESTREAM_FSSTREAM_FATSTREAM}
)

set(SRC_INOUT_BYTESTREAM_FSSTREAM_NTFSSTREAM
    "NTFSStream.cpp"
    "NTFSStream.h"
    "UncompressNTFSStream.cpp"
    "UncompressNTFSStream.h"
)

source_group(In&Out\\ByteStream\\FSStream\\NTFSStream
    FILES ${SRC_INOUT_BYTESTREAM_FSSTREAM_NTFSSTREAM}
)

set(SRC_INOUT_BYTESTREAM_MESSAGESTREAM
    "DecodeMessageStream.cpp"
    "DecodeMessageStream.h"
    "EncodeMessageStream.cpp"
    "EncodeMessageStream.h"
    "MessageStream.cpp"
    "MessageStream.h"
)

source_group(In&Out\\ByteStream\\MessageStream
    FILES ${SRC_INOUT_BYTESTREAM_MESSAGESTREAM}
)

set(SRC_INOUT_BYTESTREAM_SYSTEMSTREAM
    "FileMappingStream.cpp"
    "FileMappingStream.h"
    "FileStream.cpp"
    "FileStream.h"
    "PipeStream.cpp"
    "PipeStream.h"
    "ResourceStream.cpp"
    "ResourceStream.h"
    "SparseStream.cpp"
    "SparseStream.h"
    "ByteStreamVisitor.h"
)

source_group(In&Out\\ByteStream\\SystemStream
    FILES ${SRC_INOUT_BYTESTREAM_SYSTEMSTREAM}
)

set(SRC_INOUT_BYTESTREAM_UTILITYSTREAM
    "AccumulatingStream.cpp"
    "AccumulatingStream.h"
    "BufferStream.h"
    "ChainingStream.cpp"
    "ChainingStream.h"
    "DevNullStream.cpp"
    "DevNullStream.h"
    "JournalingStream.cpp"
    "JournalingStream.h"
    "MemoryStream.cpp"
    "MemoryStream.h"
    "MultiMemoryStream.cpp"
    "MultiMemoryStream.h"
    "StringsStream.cpp"
    "StringsStream.h"
    "TeeStream.cpp"
    "TeeStream.h"
    "TemporaryStream.cpp"
    "TemporaryStream.h"
)

source_group(In&Out\\ByteStream\\UtilityStream
    FILES ${SRC_INOUT_BYTESTREAM_UTILITYSTREAM}
)

set(SRC_INOUT_BYTESTREAM_WRAPPERSTREAM
    "DiskChunkStream.cpp"
    "DiskChunkStream.h"
    "InByteStreamWrapper.cpp"
    "InByteStreamWrapper.h"
    "ISequentialStreamWrapper.cpp"
    "ISequentialStreamWrapper.h"
    "IStreamWrapper.cpp"
    "IStreamWrapper.h"
    "OutByteStreamWrapper.cpp"
    "OutByteStreamWrapper.h"
)

source_group(In&Out\\ByteStream\\WrapperStream
    FILES ${SRC_INOUT_BYTESTREAM_WRAPPERSTREAM}
)

set(SRC_INOUT_CONCURRENT
    "BoundedBuffer.h"
    "MessageQueue.h"
    "PriorityBuffer.h"
    "Semaphore.h"
)

source_group(In&Out\\Concurrent FILES ${SRC_INOUT_CONCURRENT})

set(SRC_INOUT_DOWNLOAD
    "BITSDownloadTask.cpp"
    "BITSDownloadTask.h"
    "DownloadTask.cpp"
    "DownloadTask.h"
    "FileCopyDownloadTask.cpp"
    "FileCopyDownloadTask.h"
)

source_group(In&Out\\Download FILES ${SRC_INOUT_DOWNLOAD})

set(SRC_INOUT_IMPORT
    "ImportAgent.cpp"
    "ImportAgent.h"
    "ImportBytesSemaphore.h"
    "ImportDefinition.cpp"
    "ImportDefinition.h"
    "ImportItem.cpp"
    "ImportItem.h"
    "ImportMessage.cpp"
    "ImportMessage.h"
    "ImportNotification.cpp"
    "ImportNotification.h"
    "SqlImportAgent.cpp"
    "SqlImportAgent.h"
)

source_group(In&Out\\Import FILES ${SRC_INOUT_IMPORT})

set(SRC_INOUT_LOGPARSER "LogParserDataReader.h")

source_group(In&Out\\LogParser FILES ${SRC_INOUT_LOGPARSER})

set(SRC_INOUT_STRUCTUREDOUTPUT
    "RobustStructuredWriter.cpp"
    "RobustStructuredWriter.h"
    "StructuredOutputWriter.cpp"
    "StructuredOutputWriter.h"
)

source_group(In&Out\\StructuredOutput FILES ${SRC_INOUT_STRUCTUREDOUTPUT})

set(SRC_INOUT_STRUCTUREDOUTPUT_XML
    "XmlOutputWriter.cpp"
    "XmlOutputWriter.h"
)

source_group(In&Out\\StructuredOutput\\XML
    FILES ${SRC_INOUT_STRUCTUREDOUTPUT_XML}
)

set(SRC_INOUT_STRUCTUREDOUTPUT_JSON
    "JSONOutputWriter.cpp"
    "JSONOutputWriter.h"
)

source_group(In&Out\\StructuredOutput\\JSON
    FILES ${SRC_INOUT_STRUCTUREDOUTPUT_JSON}
)


set(SRC_INOUT_TABLEOUTPUT
    "BoundTableRecord.cpp"
    "BoundTableRecord.h"
    "TableOutput.h"
    "TableOutputExtension.cpp"
    "TableOutputExtension.h"
    "TableOutputWriter.cpp"
    "TableOutputWriter.h"
)

source_group(In&Out\\TableOutput FILES ${SRC_INOUT_TABLEOUTPUT})

set(SRC_INOUT_TABLEOUTPUT_CSV
    "CsvCruncher.cpp"
    "CsvCruncher.h"
    "CsvDataReader.h"
    "CsvFileReader.cpp"
    "CsvFileReader.h"
    "CsvFileWriter.cpp"
    "CsvFileWriter.h"
    "CsvStream.cpp"
    "CsvStream.h"
)

source_group(In&Out\\TableOutput\\CSV FILES ${SRC_INOUT_TABLEOUTPUT_CSV})

set(SRC_INOUT_TABLEOUTPUT_PARQUET ParquetOutputWriter.h)

source_group(In&Out\\TableOutput\\Parquet
    FILES ${SRC_INOUT_TABLEOUTPUT_PARQUET}
)

set(SRC_INOUT_TABLEOUTPUT_APACHE_ORC OptRowColumnOutputWriter.h)

source_group(In&Out\\TableOutput\\ApacheOrc
    FILES ${SRC_INOUT_TABLEOUTPUT_APACHE_ORC}
)

set(SRC_INOUT_TABLEOUTPUT_SQL
    "CsvToSql.cpp"
    "CsvToSql.h"
    "SqlOutputWriter.h"
)

source_group(In&Out\\TableOutput\\SQL FILES ${SRC_INOUT_TABLEOUTPUT_SQL})

set(SRC_INOUT_UPLOAD
    "BITSAgent.cpp"
    "BITSAgent.h"
    "CopyFileAgent.cpp"
    "CopyFileAgent.h"
    "UploadAgent.cpp"
    "UploadAgent.h"
    "UploadMessage.cpp"
    "UploadMessage.h"
    "UploadNotification.cpp"
    "UploadNotification.h"
)

source_group(In&Out\\Upload FILES ${SRC_INOUT_UPLOAD})

set(SRC_OBJECT
    "FileDirectory.cpp"
    "FileDirectory.h"
    "ObjectDirectory.cpp"
    "ObjectDirectory.h"
)

source_group(Object FILES ${SRC_OBJECT})

set(SRC_REGISTRY
    "Hive.h"
    "HiveQuery.cpp"
    "HiveQuery.h"
    "RegFind.cpp"
    "RegFind.h"
    "RegFindConfig.cpp"
    "RegFindConfig.h"
    "RegistryWalker.cpp"
    "RegistryWalker.h"
)

source_group(Registry FILES ${SRC_REGISTRY})

set(SRC_RUNNINGCODE
    "Authenticode.cpp"
    "Authenticode.h"
    "AutoRuns.cpp"
    "AutoRuns.h"
    "libpehash-pe.h"
    "libpehash-peutils.cpp"
    "MSIExtension.cpp"
    "MSIExtension.h"
    "PSAPIExtension.cpp"
    "PSAPIExtension.h"
    "RunningCode.cpp"
    "RunningCode.h"
    "RunningProcesses.cpp"
    "RunningProcesses.h"
    "TaskTracker.cpp"
    "TaskTracker.h"
    "WinTrustExtension.cpp"
    "WinTrustExtension.h"
)

source_group(RunningCode FILES ${SRC_RUNNINGCODE})

set(SRC_UTILITIES
    "Convert.h"
    "LogFileWriter.cpp"
    "LogFileWriter.h"
    "OrcException.cpp"
    "OrcException.h"
)

source_group(Utilities FILES ${SRC_UTILITIES})

set(SRC_UTILITIES_MEMORY
    "BinaryBuffer.cpp"
    "BinaryBuffer.h"
    "Buffer.h"
    "CircularStorage.h"
    "HeapStorage.h"
    "ObjectStorage.h"
)

source_group(Utilities\\Memory FILES ${SRC_UTILITIES_MEMORY})

set(SRC_UTILITIES_PARAMETERS
    "OutputSpec.cpp"
    "OutputSpec.h"
    "ParameterCheck.cpp"
    "ParameterCheck.h"
    "Temporary.cpp"
    "Temporary.h"
)

source_group(Utilities\\Parameters FILES ${SRC_UTILITIES_PARAMETERS})

set(SRC_UTILITIES_RESOURCES
    "EmbeddedResource.h"
    "EmbeddedResource_Embed.cpp"
    "EmbeddedResource_Extract.cpp"
)

source_group(Utilities\\Resources FILES ${SRC_UTILITIES_RESOURCES})

set(SRC_UTILITIES_STRINGS
    "CaseInsensitive.h"
    "Strings.h"
    "Unicode.cpp"
    "Unicode.h"
    "Unicode_XmlComment.cpp"
    "Unicode_XmlElement.cpp"
    "Unicode_XmlPair.cpp"
    "Unicode_XmlString.cpp"
    "WideAnsi.cpp"
    "WideAnsi.h"
)

source_group(Utilities\\Strings FILES ${SRC_UTILITIES_STRINGS})

set(SRC_UTILITIES_SYSTEM
    "CriticalSection.h"
    "CryptoUtilities.cpp"
    "CryptoUtilities.h"
    "DriverMgmt.cpp"
    "DriverMgmt.h"
    "JobObject.cpp"
    "JobObject.h"
    "Privilege.cpp"
    "Privilege.h"
    "Robustness.cpp"
    "Robustness.h"
    "SecurityDescriptor.cpp"
    "SecurityDescriptor.h"
    "SystemDetails.cpp"
    "SystemDetails.h"
    "SystemIdentity.cpp"
    "SystemIdentity.h"
    "WMIUtil.cpp"
    "WMIUtil.h"
)

source_group(Utilities\\System FILES ${SRC_UTILITIES_SYSTEM})

set(SRC_UTILITIES_TEST
    "UnitTestHelper.cpp"
    "UnitTestHelper.h"
)

source_group(Utilities\\Test FILES ${SRC_UTILITIES_TEST})

set(SRC_UTILITIES_WINAPIHELPER
    "WinApiHelper.cpp"
    "WinApiHelper.h"
)

source_group(Utilities\\WinApi FILES ${SRC_UTILITIES_WINAPIHELPER})

add_library(OrcLib
    STATIC
        "stdafx.h"
        "stdafx.cpp"
        ${SRC_COMMAND}
        ${SRC_COMMON}
        ${SRC_CONFIG}
        ${SRC_DISK}
        ${SRC_DISK_FILESYSTEM}
        ${SRC_DISK_FILESYSTEM_FAT}
        ${SRC_DISK_FILESYSTEM_NTFS}
        ${SRC_DISK_FILESYSTEM_NTFS_FILEINFO}
        ${SRC_DISK_FILESYSTEM_NTFS_MFT}
        ${SRC_DISK_FILESYSTEM_NTFS_MFT_RECORD}
        ${SRC_DISK_FILESYSTEM_NTFS_MFT_USN}
        ${SRC_DISK_LOCATION}
        ${SRC_DISK_VOLUME}
        ${SRC_EXTENSIONLIBRARIES}
        ${SRC_EXTENSIONLIBRARIES_CHAKRA}
        ${SRC_EXTENSIONLIBRARIES_YARA}
        ${SRC_INOUT}
        ${SRC_INOUT_ARCHIVE}
        ${SRC_INOUT_ARCHIVE_CABINET}
        ${SRC_INOUT_ARCHIVE_SEVENZIP}
        ${SRC_INOUT_BYTESTREAM}
        ${SRC_INOUT_BYTESTREAM_CRYPTOSTREAM}
        ${SRC_INOUT_BYTESTREAM_FSSTREAM_FATSTREAM}
        ${SRC_INOUT_BYTESTREAM_FSSTREAM_NTFSSTREAM}
        ${SRC_INOUT_BYTESTREAM_MESSAGESTREAM}
        ${SRC_INOUT_BYTESTREAM_SYSTEMSTREAM}
        ${SRC_INOUT_BYTESTREAM_UTILITYSTREAM}
        ${SRC_INOUT_BYTESTREAM_WRAPPERSTREAM}
        ${SRC_INOUT_CONCURRENT}
        ${SRC_INOUT_DOWNLOAD}
        ${SRC_INOUT_IMPORT}
        ${SRC_INOUT_LOGPARSER}
        ${SRC_INOUT_STRUCTUREDOUTPUT}
        ${SRC_INOUT_STRUCTUREDOUTPUT_XML}
        ${SRC_INOUT_STRUCTUREDOUTPUT_JSON}
        ${SRC_INOUT_TABLEOUTPUT}
        ${SRC_INOUT_TABLEOUTPUT_CSV}
        ${SRC_INOUT_TABLEOUTPUT_PARQUET}
        ${SRC_INOUT_TABLEOUTPUT_APACHE_ORC}
        ${SRC_INOUT_TABLEOUTPUT_SQL}
        ${SRC_INOUT_UPLOAD}
        ${SRC_OBJECT}
        ${SRC_REGISTRY}
        ${SRC_RUNNINGCODE}
        ${SRC_UTILITIES}
        ${SRC_UTILITIES_MEMORY}
        ${SRC_UTILITIES_PARAMETERS}
        ${SRC_UTILITIES_RESOURCES}
        ${SRC_UTILITIES_SYSTEM}
        ${SRC_UTILITIES_STRINGS}
        ${SRC_UTILITIES_TEST}
        ${SRC_UTILITIES_WINAPIHELPER}
)

target_include_directories(OrcLib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories(OrcLib PRIVATE ${Boost_INCLUDE_DIRS})

target_link_libraries(OrcLib
    PUBLIC
        7zip::7zip
        7zip::extras
        psapi.lib
        Cabinet.lib
        Wintrust.lib
        Crypt32.lib
        fmt::fmt-header-only
        tlsh::tlsh
        VisualStudio::CppUnitTest
        yara::yara
        ws2_32.lib
        Iphlpapi.lib
)

if(ORC_BUILD_JSON)
    find_package(RapidJSON CONFIG REQUIRED)
    target_include_directories(OrcLib PRIVATE ${RAPIDJSON_INCLUDE_DIRS})
endif()

if(ORC_BUILD_SSDEEP)
    find_package(ssdeep CONFIG REQUIRED)
    target_link_libraries(OrcLib PUBLIC ssdeep::fuzzy)
endif()

 if(ORC_BUILD_CHAKRACORE)
    find_package(chakracore REQUIRED)
    target_link_libraries(OrcLib PUBLIC chakracore::chakracore-header-only)
 endif()


set_target_properties(OrcLib PROPERTIES FOLDER "${ORC_ROOT_VIRTUAL_FOLDER}")
