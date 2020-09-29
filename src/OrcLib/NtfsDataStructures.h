// MIT License
//
// Copyright (c) 2019 - Microsoft
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

//
//
// DFIR-ORC is disclosing Microsoft source code with Microsoft’s permission.
//
//

#pragma once

#include "GenDataStructure.h"

#include "Output/Text/Fmt/FILE_NAME.h"

using LCN = LONGLONG;
using PLCN = LCN*;
using VCN = LONGLONG;
using PVCN = VCN*;
using LBO = LONGLONG;
using PLBO = LBO*;
using VBO = LONGLONG;
using PVBO = VBO*;
using LSN = LARGE_INTEGER;
using PLSN = LSN*;

#pragma pack(push, 1)

struct PackedBootSector
{
    UCHAR Jump[3];
    UCHAR Oem[8];
    PackedBIOSParameterBlock PackedBpb;
    UCHAR Unused[4];
    LONGLONG NumberSectors;
    LONGLONG MftStartLcn;
    LONGLONG Mft2StartLcn;
    CHAR ClustersPerFileRecordSegment;
    UCHAR Reserved0[3];
    CHAR DefaultClustersPerIndexAllocationBuffer;
    UCHAR Reserved1[3];
    LONGLONG SerialNumber;
    ULONG Checksum;
    UCHAR BootStrap[0x200 - 0x054];
};

using PPackedBootSector = PackedBootSector*;

#pragma pack(pop)
#pragma pack(push, 4)

constexpr auto g_NTFSSignature = "NTFS    ";
constexpr auto SEQUENCE_NUMBER_STRIDE = (512);

using UPDATE_SEQUENCE_NUMBER = USHORT;
using PUPDATE_SEQUENCE_NUMBER = UPDATE_SEQUENCE_NUMBER*;
using UPDATE_SEQUENCE_ARRAY = UPDATE_SEQUENCE_NUMBER[1];

struct MFT_SEGMENT_REFERENCE
{
    ULONG SegmentNumberLowPart;
    USHORT SegmentNumberHighPart;
    USHORT SequenceNumber;
};

using PMFT_SEGMENT_REFERENCE = MFT_SEGMENT_REFERENCE*;
inline constexpr bool operator<(const MFT_SEGMENT_REFERENCE& left, const MFT_SEGMENT_REFERENCE& right)
{
    if (left.SequenceNumber != right.SequenceNumber)
        return left.SequenceNumber < right.SequenceNumber;
    if (left.SegmentNumberHighPart != right.SegmentNumberHighPart)
        return left.SegmentNumberHighPart < right.SegmentNumberHighPart;
    return left.SegmentNumberLowPart < right.SegmentNumberLowPart;
}

using FILE_REFERENCE = MFT_SEGMENT_REFERENCE;
using PFILE_REFERENCE = FILE_REFERENCE;

constexpr auto NtfsUnsafeSegmentNumber(const MFT_SEGMENT_REFERENCE UNALIGNED* fr)
{
    return ((fr)->SegmentNumberLowPart);
}

constexpr auto NtfsSegmentNumber(const MFT_SEGMENT_REFERENCE UNALIGNED* fr)
{
    return NtfsUnsafeSegmentNumber(fr);
}

constexpr auto NtfsSegmentNumber(ULONGLONG UNALIGNED* fr)
{
    return NtfsUnsafeSegmentNumber((PMFT_SEGMENT_REFERENCE UNALIGNED)fr);
}

constexpr auto NtfsFullSegmentNumber(const MFT_SEGMENT_REFERENCE UNALIGNED* fr)
{
    return (*(const ULONGLONG UNALIGNED*)(fr));
}

constexpr auto NtfsSetSegmentNumber(MFT_SEGMENT_REFERENCE UNALIGNED* fr, USHORT high, ULONG low)
{
    return ((fr)->SegmentNumberHighPart = (high), (fr)->SegmentNumberLowPart = (low), (fr)->SequenceNumber = 0);
}

constexpr auto $ROOT_FILE_REFERENCE_NUMBER = 0x0005000000000005;
constexpr auto $BADCLUS_FILE_REFERENCE_NUMBER = 0x0008000000000008;
constexpr auto $SECURE_FILE_REFERENCE_NUMBER = 0x0009000000000009;

typedef struct _MULTI_SECTOR_HEADER
{
    UCHAR Signature[4];
    USHORT UpdateSequenceArrayOffset;
    USHORT UpdateSequenceArraySize;
} MULTI_SECTOR_HEADER, *PMULTI_SECTOR_HEADER;

struct FILE_RECORD_SEGMENT_HEADER
{
    MULTI_SECTOR_HEADER MultiSectorHeader;
    ULONGLONG Reserved1;
    USHORT SequenceNumber;
    USHORT Reserved2;
    USHORT FirstAttributeOffset;
    USHORT Flags;
    ULONG Reserved3[2];
    FILE_REFERENCE BaseFileRecordSegment;
    USHORT Reserved4;
    USHORT SegmentNumberHighPart;
    ULONG SegmentNumberLowPart;
    UPDATE_SEQUENCE_ARRAY UpdateArrayForCreateOnly;
};

using PFILE_RECORD_SEGMENT_HEADER = FILE_RECORD_SEGMENT_HEADER*;

constexpr auto FILE_RECORD_SEGMENT_IN_USE = 0x0001ui16;
constexpr auto FILE_FILE_NAME_INDEX_PRESENT = 0x0002ui16;
constexpr auto FILE_SYSTEM_FILE = 0x0004ui16;
constexpr auto FILE_VIEW_INDEX_PRESENT = 0x0008ui16;

using ATTRIBUTE_TYPE_CODE = ULONG;
using PATTRIBUTE_TYPE_CODE = ATTRIBUTE_TYPE_CODE*;

constexpr auto $UNUSED = 0X0LU;
constexpr auto $STANDARD_INFORMATION = 0x10LU;
constexpr auto $ATTRIBUTE_LIST = 0x20LU;
constexpr auto $FILE_NAME = 0x30LU;
constexpr auto $OBJECT_ID = 0x40LU;
constexpr auto $SECURITY_DESCRIPTOR = 0x50LU;
constexpr auto $VOLUME_NAME = 0x60LU;
constexpr auto $VOLUME_INFORMATION = 0x70LU;
constexpr auto $DATA = 0x80LU;
constexpr auto $INDEX_ROOT = 0x90LU;
constexpr auto $INDEX_ALLOCATION = 0xA0LU;
constexpr auto $BITMAP = 0xB0LU;
constexpr auto $REPARSE_POINT = 0xC0LU;
constexpr auto $EA_INFORMATION = 0xD0LU;
constexpr auto $EA = 0xE0LU;
constexpr auto $DATA_PROPERTIES = 0xF0LU;
constexpr auto $LOGGED_UTILITY_STREAM = 0x100LU;
constexpr auto $FIRST_USER_DEFINED_ATTRIBUTE = 0x1000LU;
constexpr auto $END = 0xFFFFFFFFLU;

struct ATTRIBUTE_RECORD_HEADER
{
    ATTRIBUTE_TYPE_CODE TypeCode;
    ULONG RecordLength;
    UCHAR FormCode;
    UCHAR NameLength;
    USHORT NameOffset;
    USHORT Flags;
    USHORT Instance;
    union
    {
        struct
        {
            ULONG ValueLength;
            USHORT ValueOffset;
            UCHAR Reserved[2];
        } Resident;
        struct
        {
            VCN LowestVcn;
            VCN HighestVcn;
            USHORT MappingPairsOffset;
            UCHAR CompressionUnit;
            UCHAR Reserved[5];
            LONGLONG AllocatedLength;
            LONGLONG FileSize;
            LONGLONG ValidDataLength;
            LONGLONG TotalAllocated;
        } Nonresident;
    } Form;
};
using PATTRIBUTE_RECORD_HEADER = ATTRIBUTE_RECORD_HEADER*;

constexpr auto RESIDENT_FORM = 0x00ui8;
constexpr auto NONRESIDENT_FORM = 0x01ui8;
constexpr auto ATTRIBUTE_FLAG_COMPRESSION_MASK = 0x00FF;
constexpr auto ATTRIBUTE_FLAG_SPARSE = 0x8000;
constexpr auto ATTRIBUTE_FLAG_ENCRYPTED = 0x4000;
constexpr auto RESIDENT_FORM_INDEXED = 0x01;
constexpr auto NTFS_MAX_ATTR_NAME_LEN = 255;

struct STANDARD_INFORMATION
{
    FILETIME CreationTime;
    FILETIME LastModificationTime;
    FILETIME LastChangeTime;
    FILETIME LastAccessTime;
    ULONG FileAttributes;
    ULONG MaximumVersions;
    ULONG VersionNumber;
    ULONG ClassId;
    ULONG OwnerId;
    ULONG SecurityId;
    LONGLONG QuotaCharged;
    LONGLONG USN;
};
using PSTANDARD_INFORMATION = STANDARD_INFORMATION*;

struct ATTRIBUTE_LIST_ENTRY
{
    ATTRIBUTE_TYPE_CODE AttributeTypeCode;
    USHORT RecordLength;
    UCHAR AttributeNameLength;
    UCHAR AttributeNameOffset;
    VCN LowestVcn;
    MFT_SEGMENT_REFERENCE SegmentReference;
    USHORT Reserved;
    WCHAR AttributeName[1];
};
using PATTRIBUTE_LIST_ENTRY = ATTRIBUTE_LIST_ENTRY*;

constexpr auto FILE_NAME_POSIX = 0x00ui8;
constexpr auto FILE_NAME_WIN32 = 0x01ui8;
constexpr auto FILE_NAME_DOS83 = 0x02ui8;

struct FILE_NAME_DUPLICATED_INFORMATION
{
    LONGLONG CreationTime;
    LONGLONG LastModificationTime;
    LONGLONG LastChangeTime;
    LONGLONG LastAccessTime;
    UCHAR Reserved[0x18];
};

struct FILE_NAME
{
    FILE_REFERENCE ParentDirectory;
    FILE_NAME_DUPLICATED_INFORMATION Info;
    UCHAR FileNameLength;
    UCHAR Flags;
    WCHAR FileName[1];
};
using PFILE_NAME = FILE_NAME*;

constexpr auto NTFS_MAX_FILE_NAME_LENGTH = (255);
constexpr auto NtfsFileNameSizeFromLength(size_t LEN)
{
    return (sizeof(FILE_NAME) + LEN - sizeof(WCHAR));
}
constexpr auto NtfsFileNameSize(PFILE_NAME PFN)
{
    return (sizeof(FILE_NAME) + ((PFN)->FileNameLength - 1) * sizeof(WCHAR));
}

struct EA_INFORMATION
{
    USHORT PackedEaSize;
    USHORT NeedEaCount;
    ULONG UnpackedEaSize;
};
using PEA_INFORMATION = EA_INFORMATION*;

struct FILE_FULL_EA_INFORMATION
{
    ULONG NextEntryOffset;
    UCHAR Flags;
    UCHAR EaNameLength;
    USHORT EaValueLength;
    CHAR EaName[1];
};
using PFILE_FULL_EA_INFORMATION = FILE_FULL_EA_INFORMATION*;

struct INDEX_HEADER
{
    ULONG FirstIndexEntry;
    ULONG FirstFreeByte;
    ULONG BytesAvailable;
    UCHAR Flags;
    UCHAR Reserved[3];
};
using PINDEX_HEADER = INDEX_HEADER*;

constexpr auto INDEX_NODE = 0x01;

struct INDEX_ROOT
{
    ATTRIBUTE_TYPE_CODE IndexedAttributeType;
    UCHAR CollationRule[4];
    ULONG BytesPerIndexBuffer;
    UCHAR BlocksPerIndexBuffer;
    UCHAR Reserved[3];
    INDEX_HEADER IndexHeader;
};
using PINDEX_ROOT = INDEX_ROOT*;

struct INDEX_ALLOCATION_BUFFER
{
    MULTI_SECTOR_HEADER MultiSectorHeader;
    LSN Lsn;
    VCN ThisBlock;
    INDEX_HEADER IndexHeader;
    UPDATE_SEQUENCE_ARRAY UpdateSequenceArray;
};
using PINDEX_ALLOCATION_BUFFER = INDEX_ALLOCATION_BUFFER*;

constexpr auto DEFAULT_INDEX_BLOCK_SIZE = (0x200);
constexpr auto DEFAULT_INDEX_BLOCK_BYTE_SHIFT = (9);

struct INDEX_ENTRY
{
    union
    {
        FILE_REFERENCE FileReference;
        struct
        {
            USHORT DataOffset;
            USHORT DataLength;
            ULONG ReservedForZero;
        };
    };
    USHORT Length;
    USHORT AttributeLength;
    USHORT Flags;
    USHORT Reserved;
};
using PINDEX_ENTRY = INDEX_ENTRY*;

struct SECURITY_DESCRIPTOR_INDEX_ENTRY
{
    UINT16 DataOffset;
    UINT16 DataSize;
    UINT32 Padding;
    UINT16 IndexEntrySize;
    UINT16 KeyEntrySize;
    UINT16 Flags;
    UINT16 Padding2;
    UINT32 SecId_Key;
    UINT32 SecId_Hash;
    UINT32 SecId_Data;
    UINT32 SecurityDescriptorOffset;
    UINT32 wtF;
    UINT32 SecurityDescriptorSize;
};
using PSECURITY_DESCRIPTOR_INDEX_ENTRY = SECURITY_DESCRIPTOR_INDEX_ENTRY*;

struct SECURITY_DESCRIPTOR_ENTRY
{
    UINT32 Hash;
    UINT32 SecID;
    UINT64 OffsetEntry;
    UINT32 SizeEntry;
    SECURITY_DESCRIPTOR_RELATIVE SecurityDescriptor;
};
using PSECURITY_DESCRIPTOR_ENTRY = SECURITY_DESCRIPTOR_ENTRY*;

constexpr auto NtfsFirstSecDescIndexEntry(PINDEX_HEADER IH)
{
    return (PSECURITY_DESCRIPTOR_INDEX_ENTRY)((PCHAR)(IH) + (IH)->FirstIndexEntry);
};

constexpr auto NtfsNextSecDescIndexEntry(PSECURITY_DESCRIPTOR_INDEX_ENTRY IE)
{
    return (PSECURITY_DESCRIPTOR_INDEX_ENTRY)((PCHAR)(IE) + (ULONG)(IE)->IndexEntrySize);
}
constexpr auto INDEX_ENTRY_NODE = 0x0001ui16;
constexpr auto INDEX_ENTRY_END = 0x0002ui16;
constexpr auto INDEX_ENTRY_POINTER_FORM = 0x8000ui16;
constexpr auto NtfsIndexEntryBlock(PINDEX_ENTRY IE)
{
    return *(PLONGLONG)((PCHAR)(IE) + (ULONG)(IE)->Length - sizeof(LONGLONG));
}
constexpr auto NtfsFirstIndexEntry(PINDEX_HEADER IH)
{
    return (PINDEX_ENTRY)((PCHAR)(IH) + (IH)->FirstIndexEntry);
}
constexpr auto NtfsNextIndexEntry(PINDEX_ENTRY IE)
{
    return (PINDEX_ENTRY)((PCHAR)(IE) + (ULONG)(IE)->Length);
}

constexpr auto NtfsNextIndexEntry(const INDEX_ENTRY* IE)
{
    return (const INDEX_ENTRY*)((PCHAR)(IE) + (ULONG)(IE)->Length);
}

enum REPARSE_POINT_TYPE_AND_FLAGS
{
    Isalias = 0x20000000,
    Ishighlatency = 0x40000000,
    IsMicrosoft = 0x80000000,
    NSS = 0x68000005,
    NSSrecover = 0x68000006,
    SIS = 0x68000007,
    DFS = 0x68000008,
    Mountpoint = 0x88000003,
    HSM = 0xA8000004,
    Symboliclink = 0xE8000000
};

struct REPARSE_POINT_ATTRIBUTE
{
    DWORD TypeAndFlags;
    USHORT DataLength;
    USHORT Padding;
    BYTE Data[1];
};
using PREPARSE_POINT_ATTRIBUTE = REPARSE_POINT_ATTRIBUTE*;

struct REPARSE_POINT_DATA
{
    USHORT SubstituteNameOffset;
    USHORT SubstituteNameLength;
    USHORT PrintNameOffset;
    USHORT PrintNameLength;
    DWORD Padding;
    BYTE Data[1];
};
using PREPARSE_POINT_DATA = REPARSE_POINT_DATA*;
#pragma pack(pop)
