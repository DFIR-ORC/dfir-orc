//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Strings.h"

bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b)
{
    if (a.size() != b.size())
    {
        return false;
    }

    return std::equal(
        a.begin(), a.end(), b.begin(), [](wchar_t ca, wchar_t cb) { return std::towlower(ca) == std::towlower(cb); });
}

bool StartsWithIgnoreCase(std::wstring_view str, std::wstring_view prefix)
{
    if (str.size() < prefix.size())
    {
        return false;
    }

    return EqualsIgnoreCase(str.substr(0, prefix.size()), prefix);
}
