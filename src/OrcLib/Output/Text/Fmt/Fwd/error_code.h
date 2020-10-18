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

#include "Utils/Fwd/Iconv.h"

template <>
struct fmt::formatter<std::error_code> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const std::error_code& ec, FormatContext& ctx) -> decltype(ctx.out());
};

template <>
struct fmt::formatter<std::error_code, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const std::error_code& ec, FormatContext& ctx) -> decltype(ctx.out());
};

template <typename FormatContext>
auto fmt::formatter<std::error_code>::format(const std::error_code& ec, FormatContext& ctx) -> decltype(ctx.out())
{
    const auto message = ec.message();
    if (message.size())
    {
        return fmt::format_to(ctx.out(), "{:#x}: {}", ec.value(), message);
    }

    return fmt::format_to(ctx.out(), "{:#x}", ec.value());
}

template <typename FormatContext>
auto fmt::formatter<std::error_code, wchar_t>::format(const std::error_code& ec, FormatContext& ctx)
    -> decltype(ctx.out())
{
    const auto message = ec.message();
    if (message.size())
    {
        const auto utf16 = Orc::Utf8ToUtf16(message, Orc::kFailedConversionW);
        return fmt::format_to(ctx.out(), L"{:#x}: {}", ec.value(), utf16);
    }

    return fmt::format_to(ctx.out(), L"{:#x}", ec.value());
}
