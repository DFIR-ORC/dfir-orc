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

#include "Utils/EnumFlags.h"

namespace Orc {

enum class PartitionFlags : std::int32_t
{
    None = 0x0,
    Invalid = 0x1,
    Bootable = 0x10,
    System = 0x100,
    ReadOnly = 0x1000,
    Hidden = 0x10000,
    NoAutoMount = 0x100000
};

std::wstring ToString(PartitionFlags flags);

ENABLE_BITMASK_OPERATORS(PartitionFlags);

}  // namespace Orc
