//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl
//

#pragma once

#include "Utils/EnumFlags.h"

namespace Orc {

enum class FuzzyHashStreamAlgorithm
{
    Undefined = 0,
    SSDeep = 1 << 0,
    TLSH = 1 << 1
};

ENABLE_BITMASK_OPERATORS(FuzzyHashStreamAlgorithm);

}  // namespace Orc
