//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            Yann SALAUN
//
#pragma once

#pragma managed(push, off)

#pragma pack(push, 1)

// MBR related data

namespace Orc {

struct DiskPartition
{
    UINT8 Flag;  // 00h
    UINT8 StartTrack;  // 01h
    UINT8 StartSector;  // 02h
    UINT8 StartCylinder;  // 03h
    UINT8 SysFlag;  // 04h
    UINT8 EndTrack;  // 05h
    UINT8 EndSector;  // 06h
    UINT8 EndCylinder;  // 07h
    UINT32 SectorAddress;  // 08h
    UINT32 NumberOfSector;  // 0ch
};

using PDiskPartition = DiskPartition*;

constexpr auto MAX_BASIC_PARTITION = 4;

struct MasterBootRecord
{
    UINT8 CodeArea[440];
    UINT32 DiskSignature;
    UINT8 Nulls[2];
    DiskPartition PartitionTable[MAX_BASIC_PARTITION];
    UINT8 MBRSignature[2];
};

using PMasterBootRecord = MasterBootRecord*;

struct ExtendedBootRecord
{
    UINT8 Unused[446];
    DiskPartition PartitionTable[MAX_BASIC_PARTITION];
    UINT8 MBRSignature[2];
};

using PExtendedBootRecord = ExtendedBootRecord*;

// GPT related data

struct GPTHeader
{
    UCHAR Signature[8];
    UINT32 Revision;
    UINT32 Size;
    UINT32 Crc32;
    UINT32 Reserved1;
    LONGLONG MyLba;
    LONGLONG AlternativeLba;
    LONGLONG FirstUsableLba;
    LONGLONG LastUsableLba;
    GUID DiskGuid;
    LONGLONG PartitionEntryLba;
    UINT32 NumberOfPartitionEntries;  // normally 128
    UINT32 SizeofPartitionEntry;
    UINT32 PartitionEntryArrayCrc32;
    UCHAR Reserved2[512 - 92];  // must all be 0
};

using PGPTHeader = GPTHeader*;

struct GPTPartitionEntry
{
    GUID PartitionType;
    GUID PartitionId;
    LONGLONG FirstLba;
    LONGLONG LastLba;
    LONGLONG Flags;
    UCHAR PartitionName[128 - 0x38];  // Partition name (36 UTF-16LE code units)
};

using PTPartitionEntry = GPTPartitionEntry*;
}  // namespace Orc

#pragma pack(pop)
