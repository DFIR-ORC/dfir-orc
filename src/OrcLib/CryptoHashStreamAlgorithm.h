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

enum class CryptoHashStreamAlgorithm : char
{
    Undefined = 0,
    MD5 = 1 << 0,
    SHA1 = 1 << 1,
    SHA256 = 1 << 2
};

ENABLE_BITMASK_OPERATORS(CryptoHashStreamAlgorithm)

}  // namespace Orc
