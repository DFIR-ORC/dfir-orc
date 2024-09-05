//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <fmt/format.h>
#include <fmt/xchar.h>

#include "ConfigItem.h"

template <>
struct fmt::formatter<Orc::ConfigItem, wchar_t>
{
    constexpr auto parse(fmt::basic_format_parse_context<wchar_t>& ctx)
    {
        auto it = ctx.begin();
        while (it != ctx.end() && *it != L'}')
            ++it;

        return it;
    }

    auto format(const Orc::ConfigItem& item, fmt::wformat_context& ctx) const
    {
        const wchar_t* s = item.c_str();
        if (s == nullptr)
        {
            return fmt::format_to(ctx.out(), L"<invalid item>");
        }

        return fmt::format_to(ctx.out(), L"{}", s);
    }
};

template <>
struct fmt::formatter<Orc::ConfigItem, char>
{
    constexpr auto parse(fmt::basic_format_parse_context<char>& ctx)
    {
        auto it = ctx.begin();
        while (it != ctx.end() && *it != '}')
            ++it;

        return it;
    }

    auto format(const Orc::ConfigItem& item, fmt::format_context& ctx) const
    {
        const wchar_t* s = item.c_str();
        if (s == nullptr)
        {
            return fmt::format_to(ctx.out(), "<invalid item>");
        }

        std::error_code ec;
        const auto utf8 = Orc::ToUtf8(std::wstring_view(s), ec);
        if (ec)
        {
            return fmt::format_to(ctx.out(), "<invalid item>");
        }

        return fmt::format_to(ctx.out(), "{}", utf8);
    }
};
