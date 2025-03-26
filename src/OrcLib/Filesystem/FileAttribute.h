//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2023 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Utils/EnumFlags.h"

namespace Orc {

// From 'winnt.h'
#define ORC_FILE_ATTRIBUTE_READONLY 0x00000001
#define ORC_FILE_ATTRIBUTE_HIDDEN 0x00000002
#define ORC_FILE_ATTRIBUTE_SYSTEM 0x00000004
#define ORC_FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define ORC_FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define ORC_FILE_ATTRIBUTE_DEVICE 0x00000040
#define ORC_FILE_ATTRIBUTE_NORMAL 0x00000080
#define ORC_FILE_ATTRIBUTE_TEMPORARY 0x00000100
#define ORC_FILE_ATTRIBUTE_SPARSE_FILE 0x00000200
#define ORC_FILE_ATTRIBUTE_REPARSE_POINT 0x00000400
#define ORC_FILE_ATTRIBUTE_COMPRESSED 0x00000800
#define ORC_FILE_ATTRIBUTE_OFFLINE 0x00001000
#define ORC_FILE_ATTRIBUTE_NOT_CONTENT_INDEXED 0x00002000
#define ORC_FILE_ATTRIBUTE_ENCRYPTED 0x00004000
#define ORC_FILE_ATTRIBUTE_INTEGRITY_STREAM 0x00008000
#define ORC_FILE_ATTRIBUTE_VIRTUAL 0x00010000
#define ORC_FILE_ATTRIBUTE_NO_SCRUB_DATA 0x00020000
#define ORC_FILE_ATTRIBUTE_EA 0x00040000
#define ORC_FILE_ATTRIBUTE_PINNED 0x00080000
#define ORC_FILE_ATTRIBUTE_UNPINNED 0x00100000
//#define ORC_FILE_ATTRIBUTE_RECALL_ON_OPEN 0x00040000  // ReFS only ? Conflict with FILE_ATTRIBUTE_EA
#define ORC_FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000

//#define ORC_FILE_ATTRIBUTE_STRICTLY_SEQUENTIAL 0x00200000  // 10.0.16267.0
#define ORC_FILE_ATTRIBUTE_STRICTLY_SEQUENTIAL 0x20000000  // >= 10.0.17134.0
#define ORC_FILE_ATTRIBUTE_CASCADES_REMOTE 0x40000000

enum class FileAttribute : uint32_t
{
    kFileAttributeReadOnly = ORC_FILE_ATTRIBUTE_READONLY,
    kFileAttributeHidden = ORC_FILE_ATTRIBUTE_HIDDEN,
    kFileAttributeSystem = ORC_FILE_ATTRIBUTE_SYSTEM,
    kFileAttributeDirectory = ORC_FILE_ATTRIBUTE_DIRECTORY,
    kFileAttributeArchive = ORC_FILE_ATTRIBUTE_ARCHIVE,
    kFileAttributeDevice = ORC_FILE_ATTRIBUTE_DEVICE,
    kFileAttributeNormal = ORC_FILE_ATTRIBUTE_NORMAL,
    kFileAttributeTemporary = ORC_FILE_ATTRIBUTE_TEMPORARY,
    kFileAttributeSparseFile = ORC_FILE_ATTRIBUTE_SPARSE_FILE,
    kFileAttributeReparsePoint = ORC_FILE_ATTRIBUTE_REPARSE_POINT,
    kFileAttributeCompressed = ORC_FILE_ATTRIBUTE_COMPRESSED,
    kFileAttributeOffline = ORC_FILE_ATTRIBUTE_OFFLINE,
    kFileAttributeNotContentIndexed = ORC_FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
    kFileAttributeEncrypted = ORC_FILE_ATTRIBUTE_ENCRYPTED,
    kFileAttributeIntegrityStream = ORC_FILE_ATTRIBUTE_INTEGRITY_STREAM,
    kFileAttributeVirtual = ORC_FILE_ATTRIBUTE_VIRTUAL,
    kFileAttributeNoScrubData = ORC_FILE_ATTRIBUTE_NO_SCRUB_DATA,
    kFileAttributeEA = ORC_FILE_ATTRIBUTE_EA,
    kFileAttributePinned = ORC_FILE_ATTRIBUTE_PINNED,
    kFileAttributeUnpinned = ORC_FILE_ATTRIBUTE_UNPINNED,
    //kFileAttributeRecallOnOpen = ORC_FILE_ATTRIBUTE_RECALL_ON_OPEN,
    kFileAttributeRecallOnDataAccess = ORC_FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS,
    kFileAttributeStrictlySequential = ORC_FILE_ATTRIBUTE_STRICTLY_SEQUENTIAL,
    kFileAttributeCascadesRemote = ORC_FILE_ATTRIBUTE_CASCADES_REMOTE
};

ENABLE_BITMASK_OPERATORS(FileAttribute)

// Example: 0x880 => FILE_ATTRIBUTE_NORMAL|FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM
std::string ToString(FileAttribute attributes);

// Example: 0x880 => FILE_ATTRIBUTE_NORMAL|FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM
std::wstring ToStringW(FileAttribute attributes);

// Example: 0x880 => ....HN....S...........
std::string ToIdentifiers(FileAttribute attributes);

// Example: 0x880 => ....HN....S...........
std::wstring ToIdentifiersW(FileAttribute attributes);

// Example: 0x80 => N
char ToIdentifier(FileAttribute flags);

// Example: 0x80 => N
wchar_t ToIdentifierW(FileAttribute flags);

FileAttribute ToFileAttribute(const uint32_t attributes, std::error_code& ec);

}  // namespace Orc
