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

template <typename T>
struct fmt::formatter<Orc::Traits::ByteQuantity<T>> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::Traits::ByteQuantity<T>& quantity, FormatContext& ctx) const -> decltype(ctx.out())
    {
        std::string s;
        Orc::Text::ToString(std::back_inserter(s), quantity);
        return formatter<std::string_view>::format(s, ctx);
    }
};

template <typename T>
struct fmt::formatter<Orc::Traits::ByteQuantity<T>, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::Traits::ByteQuantity<T>& quantity, FormatContext& ctx) const -> decltype(ctx.out())
    {
        std::wstring s;
        Orc::Text::ToString(std::back_inserter(s), quantity);
        return formatter<std::wstring_view, wchar_t>::format(s, ctx);
    }
};
