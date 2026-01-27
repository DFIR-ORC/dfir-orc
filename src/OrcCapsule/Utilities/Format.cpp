//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Format.h"

#include <array>

#include <fmt/format.h>
#include <fmt/xchar.h>

std::wstring FormatHumanSize(uint64_t bytes)
{
    constexpr std::array<const wchar_t*, 6> units = {L"bytes", L"KB", L"MB", L"GB", L"TB", L"PB"};
    constexpr uint64_t kStep = 1024;

    if (bytes < kStep)
    {
        return fmt::format(L"{} bytes", bytes);
    }

    double value = static_cast<double>(bytes);
    size_t unitIndex = 0;

    while (value >= kStep && unitIndex + 1 < units.size())
    {
        value /= kStep;
        ++unitIndex;
    }

    return fmt::format(L"{:.2f} {}", value, units[unitIndex]);
}

std::wstring FormatRawBytes(uint64_t bytes)
{
    const std::wstring digits = std::to_wstring(bytes);
    std::wstring result;
    result.reserve(digits.size() + digits.size() / 3 + std::wstring_view(L" bytes").size());

    for (size_t i = 0; i < digits.size(); ++i)
    {
        if (i != 0 && (digits.size() - i) % 3 == 0)
        {
            result += L',';
        }
        result += digits[i];
    }

    return fmt::format(L"{} bytes", result);
}
