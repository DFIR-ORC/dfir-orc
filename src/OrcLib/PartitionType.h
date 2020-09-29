//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl
//

#pragma once

#include <string>

namespace Orc {

enum class PartitionType
{
    Invalid,
    FAT12,
    FAT16,
    FAT32,
    NTFS,
    REFS,
    Extended,
    GPT,
    ESP,
    MICROSOFT_RESERVED,
    BitLocked,
    Other
};

std::wstring ToString(PartitionType type);

}  // namespace Orc
