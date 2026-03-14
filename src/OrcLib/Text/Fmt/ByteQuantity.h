//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <array>
#include <string>

#include "Text/ByteQuantity.h"

template <typename T, typename Char>
struct fmt::formatter<Orc::Traits::ByteQuantity<T>, Char>
{
    constexpr auto parse(fmt::basic_format_parse_context<Char>& ctx) { return ctx.begin(); }

    template <typename FormatContext>
    auto format(const Orc::Traits::ByteQuantity<T>& quantity, FormatContext& ctx) const
    {
        Orc::Text::ToString(ctx.out(), quantity);
        return ctx.out();
    }
};
