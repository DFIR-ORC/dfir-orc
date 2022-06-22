//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/Format.h"
#include "Text/Iconv.h"
#include <PartitionFlags.h>

template <>
struct fmt::formatter<Orc::PartitionFlags> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::PartitionFlags& flags, FormatContext& ctx) -> decltype(ctx.out())
    {
        std::error_code ec;
        const auto utf8 = Orc::Utf16ToUtf8(Orc::ToString(flags), ec);
        if (ec)
        {
            return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
        }

        return formatter<std::string_view>::format(utf8, ctx);
    }
};

template <>
struct fmt::formatter<Orc::PartitionFlags, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::PartitionFlags& flags, FormatContext& ctx) -> decltype(ctx.out())
    {
        return formatter<std::wstring_view, wchar_t>::format(Orc::ToString(flags), ctx);
    }
};
