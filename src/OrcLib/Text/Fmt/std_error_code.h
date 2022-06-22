//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <system_error>

#include <fmt/format.h>

#include "Text/Iconv.h"

template <>
struct fmt::formatter<std::error_code> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const std::error_code& ec, FormatContext& ctx) -> decltype(ctx.out())
    {
        std::string s;

        if (ec.category() == std::system_category())
        {
            fmt::format_to(std::back_inserter(s), "{:#x}", static_cast<uint32_t>(ec.value()));
        }
        else
        {
            fmt::format_to(std::back_inserter(s), "{}", ec.value());
        }

        auto message = ec.message();
        if (!message.empty())
        {
            auto pos = message.rfind("\r\n");
            if (pos != std::string::npos && pos == message.size() - 2)
            {
                message = message.substr(0, pos);
            }

            fmt::format_to(std::back_inserter(s), ": {}", message);
        }

        return formatter<std::string_view>::format(s, ctx);
    }
};

template <>
struct fmt::formatter<std::error_code, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const std::error_code& ec, FormatContext& ctx) -> decltype(ctx.out())
    {
        using namespace std::string_view_literals;
        std::wstring s;

        if (ec.category() == std::system_category())
        {
            fmt::format_to(std::back_inserter(s), L"{:#x}"sv, static_cast<uint32_t>(ec.value()));
        }
        else
        {
            fmt::format_to(std::back_inserter(s), L"{}"sv, ec.value());
        }

        auto message = ec.message();
        if (!message.empty())
        {
            auto pos = message.rfind("\r\n");
            if (pos != std::string::npos && pos == message.size() - 2)
            {
                message = message.substr(0, pos);
            }

            const auto utf16 = Orc::Utf8ToUtf16(message);
            fmt::format_to(std::back_inserter(s), L": {}"sv, utf16);
        }

        return formatter<std::wstring_view, wchar_t>::format(s, ctx);
    }
};
