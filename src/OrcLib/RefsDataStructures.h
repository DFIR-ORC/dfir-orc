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

typedef struct _PackedRefsV1BootSector
{

    UCHAR Jump[3];  //  offset = 0x000
    UCHAR Oem[8];  //  offset = 0x003
    UCHAR NotIdentifiedYet[13];  //  offset = 0x00B
    LONGLONG NumberSectors;  //  offset = 0x018
    ULONG SectorSize;  //  offset = 0x020
    ULONG ClusterSize;  //  offset = 0x024
    UCHAR NotIdentifiedYet2[0x200 - 0x028];  //  offset = 0x028
} PackedRefsV1BootSector;
typedef PackedRefsV1BootSector* PPackedRefsV1BootSector;

#pragma pack(pop)

#pragma pack(push, 4)

const UCHAR g_REFSSignature[] = "ReFS    ";

#pragma pack(pop)

#pragma managed(pop)
