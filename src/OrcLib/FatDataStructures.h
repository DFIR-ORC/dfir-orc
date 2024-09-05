//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#pragma once

#include "GenDataStructure.h"

#pragma managed(push, off)

#pragma pack(push, 1)

namespace Orc {

struct PackedGenFatBootSector
{
    UCHAR Jump[3];  //  offset = 0x000
    UCHAR Oem[8];  //  offset = 0x003
    PackedBIOSParameterBlock PackedBpb;  //  offset = 0x00B
};
using PPackedGenFatBootSector = PackedGenFatBootSector*;

struct PackedFat1216BootSector
{
    PackedGenFatBootSector PackedGenFatBootSector;
    UCHAR BiosDrive;
    UCHAR Reserved;
    UCHAR Signature;
    DWORD VolumeSerialNumber;
    UCHAR VolumeLabel[11];
    UCHAR SystemId[8];
};
using PPackedFat1216BootSector = PackedFat1216BootSector*;

struct PackedFat32BootSector
{
    PackedGenFatBootSector PackedGenFatBootSector;
    DWORD NumberOfSectorsPerFat;
    WORD Flags;
    WORD Version;
    DWORD RootDirectoryClusterNumber;
    WORD SectorNumberofFileSystemInformation;
    WORD SectorNumberOfBackupBoot;
    UCHAR Reserved[12];
    UCHAR PhysicalDiskNumber;
    UCHAR Unused;
    UCHAR Signature;
    DWORD VolumeSerialNumber;
    UCHAR VolumeLabel[11];
    UCHAR SystemId[8];
};
using PPackedFat32BootSector = PackedFat32BootSector*;

struct GenFatFile
{
    UCHAR FirstByte;
    UCHAR NextBytes[10];
    UCHAR Attributes;
};

struct FatFile83
{
    CHAR Filename[8];
    CHAR Extension[3];
    UCHAR Attributes;
    UCHAR Reserved;
    UCHAR CreationTimeMs;
    WORD CreationTime;
    WORD CreationDate;
    WORD LastAccessDate;
    WORD FirstClusterHigh2Bytes;
    WORD ModifiedTime;
    WORD ModifiedDate;
    WORD FirstClusterLow2Bytes;
    DWORD FileSize;
};

struct FatFileLFN
{
    UCHAR SequenceNumber;
    WCHAR Name1[5];
    UCHAR Attributes;  // Always 0x0f
    UCHAR Reserved;  // Always 0x00
    UCHAR Checksum;  // Checksum of DOS Filename.  See Docs.
    WCHAR Name2[6];
    WORD FirstCluster;  // Always 0x0000
    WCHAR Name3[2];
};

}  // namespace Orc

#pragma pack(pop)

#pragma managed(pop)
