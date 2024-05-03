//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Utils/TypeTraits.h"

// TODO: have on FormatOffsetTo function instead of two

template <typename OutputIt, typename T>
void FormatOffsetTo(OutputIt out, const Orc::Traits::Offset<T>& offset)
{
    if constexpr (sizeof(T) == 4)
    {
        fmt::format_to(out, "{:#010x}", offset.value);
    }
    else if constexpr (sizeof(T) == 8)
    {
        fmt::format_to(out, "{:#018x}", offset.value);
    }
    else
    {
        fmt::format_to(out, "{:#x}", offset.value);
    }
}

template <typename OutputIt, typename T>
void FormatOffsetToW(OutputIt out, const Orc::Traits::Offset<T>& offset)
{
    if constexpr (sizeof(T) == 4)
    {
        fmt::format_to(out, L"{:#010x}", offset.value);
    }
    else if constexpr (sizeof(T) == 8)
    {
        fmt::format_to(out, L"{:#018x}", offset.value);
    }
    else
    {
        fmt::format_to(out, L"{:#x}", offset.value);
    }
}

template <typename T>
struct fmt::formatter<Orc::Traits::Offset<T>> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::Traits::Offset<T>& offset, FormatContext& ctx) const -> decltype(ctx.out())
    {
        std::string s;
        FormatOffsetTo(std::back_inserter(s), offset);
        return formatter<std::string_view>::format(s, ctx);
    }
};

template <typename T>
struct fmt::formatter<Orc::Traits::Offset<T>, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::Traits::Offset<T>& offset, FormatContext& ctx) const -> decltype(ctx.out())
    {
        std::wstring s;
        FormatOffsetToW(std::back_inserter(s), offset);
        return formatter<std::wstring_view, wchar_t>::format(s, ctx);
    }
};
