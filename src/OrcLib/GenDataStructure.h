//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#pragma managed(push, off)

#pragma pack(push, 1)

struct PackedGenBootSector
{

    UCHAR Jump[3];  //  offset = 0x000
    UCHAR Oem[8];  //  offset = 0x003
    UCHAR Other[0x200 - 0x0B];  //  offset = 0xB
};
using PPackedGenBootSector = PackedGenBootSector*;

struct PackedBIOSParameterBlock
{

    WORD BytesPerSector;  //  offset = 0x000
    UCHAR SectorsPerCluster;  //  offset = 0x002
    WORD ReservedSectors;  //  offset = 0x003 (zero)
    UCHAR Fats;  //  offset = 0x005 (zero)
    WORD RootEntries;  //  offset = 0x006 (zero)
    WORD Sectors;  //  offset = 0x008 (zero)
    UCHAR Media;  //  offset = 0x00A
    WORD SectorsPerFat;  //  offset = 0x00B (zero)
    WORD SectorsPerTrack;  //  offset = 0x00D
    WORD Heads;  //  offset = 0x00F
    DWORD HiddenSectors;  //  offset = 0x011 (zero)
    DWORD LargeSectors;  //  offset = 0x015 (zero)
};  //  sizeof = 0x019

using PPackedBIOSParameterBlock = PackedBIOSParameterBlock*;

#pragma pack(pop)

#pragma managed(pop)
