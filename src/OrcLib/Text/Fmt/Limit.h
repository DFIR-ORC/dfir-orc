//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/Fmt/Fwd/Limit.h"

#include "Text/Fmt/ByteQuantity.h"
#include <Limit.h>

template <typename T>
template <typename FormatContext>
auto fmt::formatter<Orc::Limit<T>>::format(const Orc::Limit<T>& limit, FormatContext& ctx) -> decltype(ctx.out())
{
    if (limit.IsUnlimited())
    {
        return formatter<std::string_view>::format("Unlimited", ctx);
    }

    const auto value = fmt::format("{}", limit.value);
    return formatter<std::string_view>::format(value, ctx);
}

template <typename T>
template <typename FormatContext>
auto fmt::formatter<Orc::Limit<T>, wchar_t>::format(const Orc::Limit<T>& limit, FormatContext& ctx)
    -> decltype(ctx.out())
{
    if (limit.IsUnlimited())
    {
        return formatter<std::wstring_view, wchar_t>::format(L"Unlimited", ctx);
    }

    const auto value = fmt::format(L"{}", limit.value);
    return formatter<std::wstring_view, wchar_t>::format(value, ctx);
}
