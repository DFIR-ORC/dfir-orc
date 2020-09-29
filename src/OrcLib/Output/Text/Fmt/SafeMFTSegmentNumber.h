//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <fmt/format.h>

#include "MFTUtils.h"

template <>
struct fmt::formatter<Orc::MFTUtils::SafeMFTSegmentNumber> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::MFTUtils::SafeMFTSegmentNumber& frn, FormatContext& ctx)
    {
        fmt::format_to(ctx.out(), "{:#018x}", frn);
        return ctx;
    }
};

template <>
struct fmt::formatter<Orc::MFTUtils::SafeMFTSegmentNumber, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::MFTUtils::SafeMFTSegmentNumber& ft, FormatContext& ctx)
    {
        fmt::format_to(ctx.out(), L"{:#018x}", frn);
        return ctx;
    }
};
