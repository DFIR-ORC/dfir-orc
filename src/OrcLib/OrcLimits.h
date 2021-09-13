//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <functional>

#include <windows.h>

#include "Utils/TypeTraits.h"
#include "Limit.h"

namespace Orc {

class Limits
{
public:
    Limits() = default;

    bool bIgnoreLimits = false;

    Limit<typename Traits::ByteQuantity<DWORDLONG>> dwlMaxTotalBytes = INFINITE;
    DWORDLONG dwlAccumulatedBytesTotal = 0LL;
    bool bMaxTotalBytesReached = false;

    Limit<Traits::ByteQuantity<DWORDLONG>> dwlMaxBytesPerSample = INFINITE;
    bool bMaxBytesPerSampleReached = false;

    Limit<DWORD> dwMaxSampleCount = INFINITE;
    DWORD dwAccumulatedSampleCount = 0LL;
    bool bMaxSampleCountReached = false;
};

}  // namespace Orc
