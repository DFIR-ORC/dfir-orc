//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
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

    std::optional<Limit<typename Traits::ByteQuantity<DWORDLONG>>> dwlMaxTotalBytes;
    DWORDLONG dwlAccumulatedBytesTotal = 0LL;
    bool bMaxTotalBytesReached = false;

    std::optional<Limit<Traits::ByteQuantity<DWORDLONG>>> dwlMaxBytesPerSample;
    bool bMaxBytesPerSampleReached = false;

    std::optional<Limit<DWORD>> dwMaxSampleCount;
    DWORD dwAccumulatedSampleCount = 0LL;
    bool bMaxSampleCountReached = false;
};

}  // namespace Orc
