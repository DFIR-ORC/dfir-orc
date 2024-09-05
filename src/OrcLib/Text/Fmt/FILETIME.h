//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/Fmt/SYSTEMTIME.h"

// All time displayed are UTC so a FILETIME must converted to UTC (SYSTEMTIME)
template <>
struct fmt::formatter<FILETIME> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const FILETIME& ft, FormatContext& ctx) const -> decltype(ctx.out())
    {
        SYSTEMTIME stUTC {0};
        if (FileTimeToSystemTime(&ft, &stUTC))
            return formatter<std::string_view>::format(Orc::Text::FormatSystemTimeA(stUTC), ctx);
        return ctx.out();
    }
};

template <>
struct fmt::formatter<FILETIME, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const FILETIME& ft, FormatContext& ctx) const -> decltype(ctx.out())
    {
        SYSTEMTIME stUTC {0};
        if (FileTimeToSystemTime(&ft, &stUTC))
            return formatter<std::wstring_view, wchar_t>::format(Orc::Text::FormatSystemTimeW(stUTC), ctx);
        return ctx.out();
    }
};
