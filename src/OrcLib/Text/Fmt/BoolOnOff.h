//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <fmt/format.h>
#include <fmt/xchar.h>

#include "Utils/TypeTraits.h"

template <>
struct fmt::formatter<Orc::Traits::BoolOnOff> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::Traits::BoolOnOff& value, FormatContext& ctx) const -> decltype(ctx.out())
    {
        return formatter<std::string_view>::format(value ? "On" : "Off", ctx);
    }
};

template <>
struct fmt::formatter<Orc::Traits::BoolOnOff, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::Traits::BoolOnOff& value, FormatContext& ctx) const -> decltype(ctx.out())
    {
        return formatter<std::wstring_view, wchar_t>::format(value ? L"On" : L"Off", ctx);
    }
};
