//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/Fmt/Fwd/PartitionType.h"

#include "Text/Format.h"
#include "Text/Iconv.h"
#include <PartitionType.h>

template <typename FormatContext>
auto fmt::formatter<Orc::PartitionType>::format(const Orc::PartitionType& type, FormatContext& ctx)
    -> decltype(ctx.out())
{
    std::error_code ec;
    const auto utf8 = Orc::Utf16ToUtf8(Orc::ToString(type), ec);
    if (ec)
    {
        return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
    }

    return formatter<std::string_view>::format(utf8, ctx);
}

template <typename FormatContext>
auto fmt::formatter<Orc::PartitionType, wchar_t>::format(const Orc::PartitionType& type, FormatContext& ctx)
    -> decltype(ctx.out())
{
    return formatter<std::wstring_view, wchar_t>::format(Orc::ToString(type), ctx);
}
