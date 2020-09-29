//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Output/Text/Fmt/Fwd/PartitionFlags.h"

#include "Output/Text/Format.h"
#include "Utils/Iconv.h"
#include <PartitionFlags.h>

template <typename FormatContext>
auto fmt::formatter<Orc::PartitionFlags>::format(const Orc::PartitionFlags& flags, FormatContext& ctx)
    -> decltype(ctx.out())
{
    std::error_code ec;
    const auto utf8 = Orc::Utf16ToUtf8(Orc::ToString(flags), ec);
    if (ec)
    {
        return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
    }

    return formatter<std::string_view>::format(utf8, ctx);
}

template <typename FormatContext>
auto fmt::formatter<Orc::PartitionFlags, wchar_t>::format(const Orc::PartitionFlags& flags, FormatContext& ctx)
    -> decltype(ctx.out())
{
    return formatter<std::wstring_view, wchar_t>::format(Orc::ToString(flags), ctx);
}
