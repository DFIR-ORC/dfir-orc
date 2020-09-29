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
#include "Output/Text/Fmt/SystemTime.h"

// All time displayed are UTC so a FILETIME must converted to UTC (SYSTEMTIME)

template <>
struct fmt::formatter<FILETIME> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const FILETIME& ft, FormatContext& ctx)
    {
        SYSTEMTIME stUTC;
        FileTimeToSystemTime(&ft, &stUTC);
        return Orc::Text::FormatSystemTimeA(stUTC, ctx);
    }
};

template <>
struct fmt::formatter<FILETIME, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const FILETIME& ft, FormatContext& ctx)
    {
        SYSTEMTIME stUTC;
        FileTimeToSystemTime(&ft, &stUTC);
        return Orc::Text::FormatSystemTimeW(stUTC, ctx);
    }
};
