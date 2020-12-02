//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "WolfLauncher.h"

#include <system_error>

#include "Output/Text/Format.h"
#include "Utils/Iconv.h"

template<>
struct fmt::formatter<Orc::Command::Wolf::Main::WolfPriority> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::Command::Wolf::Main::WolfPriority& priority, FormatContext& ctx) -> decltype(ctx.out())
    {
        std::error_code ec;
        const auto utf8 = Orc::Utf16ToUtf8(Orc::Command::Wolf::WolfLauncher::ToString(priority), ec);
        if (ec)
        {
            return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
        }

        return formatter<std::string_view>::format(utf8, ctx);
    }
};

template<>
struct fmt::formatter<Orc::Command::Wolf::Main::WolfPriority, wchar_t>
    : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(Orc::Command::Wolf::Main::WolfPriority priority, FormatContext& ctx) -> decltype(ctx.out())
    {
        return formatter<std::wstring_view, wchar_t>::format(Orc::Command::Wolf::Main::ToString(priority), ctx);
    }
};
