//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl
//

#include "stdafx.h"

#include "PartitionType.h"

namespace Orc {

std::wstring ToString(PartitionType type)
{
    switch (type)
    {
        case PartitionType::BitLocked:
            return L"Bitlocked";
        case PartitionType::ESP:
            return L"ESP";
        case PartitionType::Extended:
            return L"Extended";
        case PartitionType::FAT12:
            return L"FAT12";
        case PartitionType::FAT16:
            return L"FAT16";
        case PartitionType::FAT32:
            return L"FAT32";
        case PartitionType::GPT:
            return L"GPT";
        case PartitionType::Invalid:
            return L"Invalid";
        case PartitionType::MICROSOFT_RESERVED:
            return L"MICROSOFT_RESERVED";
        case PartitionType::NTFS:
            return L"NTFS";
        case PartitionType::Other:
            return L"Other";
        case PartitionType::REFS:
            return L"REFS";
    }

    return L"Unsupported";
}

}  // namespace Orc
