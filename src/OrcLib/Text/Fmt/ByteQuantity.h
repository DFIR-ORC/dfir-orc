//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <array>
#include <string>

#include "Utils/TypeTraits.h"

template <typename OutputIt, typename T>
void FormatByteQuantityTo(OutputIt out, const Orc::Traits::ByteQuantity<T>& quantity)
{
    constexpr std::array units = {"B", "KB", "MB", "GB", "TB"};
    const size_t step = 1000;

    T value = quantity.value;
    size_t index = 0;
    for (; index < units.size() - 1; ++index)
    {
        if (value < step)
        {
            break;
        }

        value = value / step;
    }

    fmt::format_to(out, "{} {}", value, units[index]);
}

template <typename OutputIt, typename T>
void FormatByteQuantityToW(OutputIt out, const Orc::Traits::ByteQuantity<T>& quantity)
{
    constexpr std::array units = {L"B", L"KB", L"MB", L"GB", L"TB"};
    const size_t step = 1000;

    T value = quantity.value;
    size_t index = 0;
    for (; index < units.size() - 1; ++index)
    {
        if (value < step)
        {
            break;
        }

        value = value / step;
    }

    fmt::format_to(out, L"{} {}", value, units[index]);
}

template <typename T>
struct fmt::formatter<Orc::Traits::ByteQuantity<T>> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::Traits::ByteQuantity<T>& quantity, FormatContext& ctx) -> decltype(ctx.out())
    {
        std::string s;
        FormatByteQuantityTo(std::back_inserter(s), quantity);
        return formatter<std::string_view>::format(s, ctx);
    }
};

template <typename T>
struct fmt::formatter<Orc::Traits::ByteQuantity<T>, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::Traits::ByteQuantity<T>& quantity, FormatContext& ctx) -> decltype(ctx.out())
    {
        std::wstring s;
        FormatByteQuantityToW(std::back_inserter(s), quantity);
        return formatter<std::wstring_view, wchar_t>::format(s, ctx);
    }
};
