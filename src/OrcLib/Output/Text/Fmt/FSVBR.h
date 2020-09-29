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
#include <FSVBR.h>

template <>
struct fmt::formatter<Orc::FSVBR::FSType> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::FSVBR::FSType& FSType, FormatContext& ctx)
    {
        const auto utf16 = Orc::FSVBR::ToString(FSType);

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
struct fmt::formatter<Orc::FSVBR::FSType, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::FSVBR::FSType& FSType, FormatContext& ctx)
    {
        const auto utf16 = Orc::FSVBR::ToString(FSType);
        std::copy(std::cbegin(utf16), std::cend(utf16), ctx.out());
        return ctx.out();
    }
};
