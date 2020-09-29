//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <windows.h>

#include <fmt/format.h>

#include "Output/Text/Format.h"
#include <Location.h>

template <>
struct fmt::formatter<Orc::Location::Type> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::Location::Type& type, FormatContext& ctx)
    {
        const auto utf16 = Orc::Location::ToString(type);

        std::error_code ec;
        const auto utf8 = Utf16ToUtf8(utf16, ec);
        if (ec)
        {
            return fmt::format_to(ctx.out(), "<failed_conversion>");
        }

        std::copy(std::cbegin(utf8), std::cend(utf8), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::Location::Type, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::Location::Type& type, FormatContext& ctx)
    {
        const auto utf16 = Orc::Location::ToString(type);
        std::copy(std::cbegin(utf16), std::cend(utf16), ctx.out());
        return ctx.out();
    }
};
