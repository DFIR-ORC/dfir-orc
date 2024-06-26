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
#include <fmt/xchar.h>

template <>
struct fmt::formatter<std::error_code> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const std::error_code& ec, FormatContext& ctx) const -> decltype(ctx.out())
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
    auto format(const std::error_code& ec, FormatContext& ctx) const -> decltype(ctx.out())
    {
        std::wstring s;

        if (ec.category() == std::system_category())
        {
            fmt::format_to(std::back_inserter(s), L"{:#x}", static_cast<uint32_t>(ec.value()));
        }
        else
        {
            fmt::format_to(std::back_inserter(s), L"{}", ec.value());
        }

        auto message = ec.message();
        if (!message.empty())
        {
            auto pos = message.rfind("\r\n");
            if (pos != std::string::npos && pos == message.size() - 2)
            {
                message = message.substr(0, pos);
            }

            // It is expected that ec.message() only return ansi character set
            std::wstring utf16;
            std::copy(std::cbegin(message), std::cend(message), std::back_inserter(utf16));
            fmt::format_to(std::back_inserter(s), L": {}", utf16);
        }

        return formatter<std::wstring_view, wchar_t>::format(s, ctx);
    }
};
