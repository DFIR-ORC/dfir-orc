//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "FSVBR_FSType.h"
#include "Text/Format.h"

#include <FSVBR.h>

template <>
struct fmt::formatter<Orc::FSVBR_FSType> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::FSVBR_FSType& FSType, FormatContext& ctx) const -> decltype(ctx.out())
    {
        const auto utf16 = Orc::ToString(FSType);

        std::error_code ec;
        const auto utf8 = Orc::ToUtf8(utf16, ec);
        if (ec)
        {
            return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
        }

        return formatter<std::string_view>::format(utf8, ctx);
    }
};

template <>
struct fmt::formatter<Orc::FSVBR_FSType, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::FSVBR_FSType& FSType, FormatContext& ctx) const -> decltype(ctx.out())
    {
        return formatter<std::wstring, wchar_t>::format(Orc::ToString(FSType), ctx);
    }
};
