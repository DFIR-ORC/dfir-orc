//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Utils/TypeTraits.h"

template <typename OutputIt, typename T>
void FormatOffsetTo(OutputIt out, const Orc::Traits::Offset<T>& offset)
{
    using Char = Orc::Traits::underlying_char_type_t<std::remove_cv_t<std::remove_reference_t<OutputIt>>>;

    if constexpr (std::is_same_v<Char, wchar_t>)
    {
        if constexpr (sizeof(T) == 4)
            fmt::format_to(out, L"{:#010x}", offset.value);
        else if constexpr (sizeof(T) == 8)
            fmt::format_to(out, L"{:#018x}", offset.value);
        else
            fmt::format_to(out, L"{:#x}", offset.value);
    }
    else
    {
        if constexpr (sizeof(T) == 4)
            fmt::format_to(out, "{:#010x}", offset.value);
        else if constexpr (sizeof(T) == 8)
            fmt::format_to(out, "{:#018x}", offset.value);
        else
            fmt::format_to(out, "{:#x}", offset.value);
    }
}

template <typename T, typename Char>
struct fmt::formatter<Orc::Traits::Offset<T>, Char>
{
    constexpr auto parse(fmt::basic_format_parse_context<Char>& ctx) { return ctx.begin(); }

    template <typename FormatContext>
    auto format(const Orc::Traits::Offset<T>& offset, FormatContext& ctx) const
    {
        FormatOffsetTo(ctx.out(), offset);
        return ctx.out();
    }
};
