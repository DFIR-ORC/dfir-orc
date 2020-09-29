//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Output/Text/Fmt/Fwd/FILETIME.h"

#include "Output/Text/Fmt/SYSTEMTIME.h"

// All time displayed are UTC so a FILETIME must converted to UTC (SYSTEMTIME)

template <typename FormatContext>
auto fmt::formatter<FILETIME>::format(const FILETIME& ft, FormatContext& ctx) -> decltype(ctx.out())
{
    SYSTEMTIME stUTC;
    FileTimeToSystemTime(&ft, &stUTC);
    return formatter<std::string_view>::format(Orc::Text::FormatSystemTimeA(stUTC), ctx);
}

template <typename FormatContext>
auto fmt::formatter<FILETIME, wchar_t>::format(const FILETIME& ft, FormatContext& ctx) -> decltype(ctx.out())
{
    SYSTEMTIME stUTC;
    FileTimeToSystemTime(&ft, &stUTC);
    return formatter<std::wstring_view, wchar_t>::format(Orc::Text::FormatSystemTimeW(stUTC), ctx);
}
