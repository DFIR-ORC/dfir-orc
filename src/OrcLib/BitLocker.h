//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
// Based on work from Darell Tan: https://github.com/geekman/blwipe
//                                Copyright (C) 2018 Darell Tan
//

#pragma once

#pragma managed(push, off)

#pragma pack(push, 1)

#include <cstdint>
#define INITGUID
#include <guiddef.h>

namespace Orc::BitLocker {

struct VolumeHeader
{
    uint8_t Jmp[3];
    uint8_t Signature[8];
    uint16_t SectorSize;
    uint8_t SectorsPerCluster;
    uint16_t ReservedClusters;

    // unimportant fields
    uint8_t Unknown1[1 + 2 + 2 + 1 + 2 + 2 + 2 + 4 + 4];
    uint8_t Unknown2[4];

    uint64_t NumSectors;
    uint64_t MftStartCluster;
    uint64_t MetadataLcn;

    uint8_t Unknwon3[96];

    GUID Guid;
    uint64_t InfoOffsets[3];
    uint64_t EOWOffsets[2];
};

struct ValidationHeader
{
    uint16_t Size;
    uint16_t Version;
    uint32_t Crc32;
};

struct InfoStructHeader
{
    uint8_t Signature[8];
    uint16_t Size;
    uint16_t Version;
};

struct InfoStruct
{
    InfoStructHeader Header;
    uint8_t Unknown1[2 + 2];
    uint64_t VolumeSize;

    uint32_t ConvertSize;
    uint32_t HeaderSectors;
    uint64_t InfoOffsets[3];
    uint64_t HeaderSectorsOffset;
};

DEFINE_GUID(INFO_GUID, 0x4967D63B, 0x2E29, 0x4AD8, 0x83, 0x99, 0xF6, 0xA3, 0x39, 0xE3, 0xD0, 0x01);

}  // namespace Orc::BitLocker
