//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2024 ANSSI. All Rights Reserved.
//
// Author(s): Copilot (refactor)
//

#include "WolfPriority.h"

namespace Orc {

std::wstring ToString(WolfPriority value)
{
    switch (value)
    {
        case WolfPriority::Low:
            return L"Low";
        case WolfPriority::Normal:
            return L"Normal";
        case WolfPriority::High:
            return L"High";
        case WolfPriority::Idle:
            return L"Idle";
        default:
            return L"<Unknown>";
    }
}

} // namespace Orc
