//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/Fmt/Fwd/FSVBR.h"
#include "Text/Format.h"

#include <FSVBR.h>

#include <windows.h>

template <typename FormatContext>
auto fmt::formatter<Orc::FSVBR_FSType>::format(const Orc::FSVBR_FSType& FSType, FormatContext& ctx)
    -> decltype(ctx.out())
{
    const auto utf16 = Orc::ToString(FSType);

    std::error_code ec;
    const auto utf8 = Orc::Utf16ToUtf8(utf16, ec);
    if (ec)
    {
        return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
    }

    formatter<std::string_view>::format(utf8, ctx);
}

template <typename FormatContext>
auto fmt::formatter<Orc::FSVBR_FSType, wchar_t>::format(const Orc::FSVBR_FSType& FSType, FormatContext& ctx)
    -> decltype(ctx.out())
{
    return formatter<std::wstring, wchar_t>::format(Orc::ToString(FSType), ctx);
}
