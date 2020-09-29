//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "Utils/EnumFlags.h"

namespace Orc {

enum class FSVBR_FSType : std::int32_t
{
    UNKNOWN = 0,
    FAT12 = 0x1,
    FAT16 = 0x2,
    FAT32 = 0x4,
    FAT = 0x7,
    NTFS = 0x8,
    REFS = 0x10,
    BITLOCKER = 0x20,
    ALL = 0xFFFFFFF
};

ENABLE_BITMASK_OPERATORS(FSVBR_FSType);

static std::wstring ToString(FSVBR_FSType fsType)
{
    using FSType = FSVBR_FSType;

    switch (fsType)
    {
        case FSType::ALL:
            return L"ALL";
        case FSType::BITLOCKER:
            return L"Bit Locker";
        case FSType::FAT:
            return L"FAT";
        case FSType::FAT12:
            return L"FAT12";
        case FSType::FAT16:
            return L"FAT16";
        case FSType::FAT32:
            return L"FAT32";
        case FSType::NTFS:
            return L"NTFS";
        case FSType::REFS:
            return L"REFS";
        case FSType::UNKNOWN:
            return L"Unknown";
    }

    return L"Unsupported";
}

}  // namespace Orc
