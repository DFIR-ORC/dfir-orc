//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "WolfPriority.h"

#include <system_error>

#include "Text/Format.h"
#include "Text/Iconv.h"

template <>
struct fmt::formatter<Orc::WolfPriority> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::WolfPriority& priority, FormatContext& ctx) const -> decltype(ctx.out())
    {
        std::error_code ec;
        const auto utf8 = Orc::ToUtf8(Orc::ToString(priority), ec);
        if (ec)
        {
            return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
        }
        return formatter<std::string_view>::format(utf8, ctx);
    }
};

template <>
struct fmt::formatter<Orc::WolfPriority, wchar_t>
    : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(Orc::WolfPriority priority, FormatContext& ctx) const -> decltype(ctx.out())
    {
        return formatter<std::wstring_view, wchar_t>::format(Orc::ToString(priority), ctx);
    }
};
