//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <fmt/format.h>
#include "Text/Fmt/ByteQuantity.h"
#include <Limit.h>

template <typename T>
struct fmt::formatter<Orc::Limit<T>> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::Limit<T>& limit, FormatContext& ctx) const -> decltype(ctx.out())
    {
        if (limit.IsUnlimited())
        {
            return formatter<std::string_view>::format("Unlimited", ctx);
        }

        const auto value = fmt::format("{}", limit.value);
        return formatter<std::string_view>::format(value, ctx);
    }
};

template <typename T>
struct fmt::formatter<Orc::Limit<T>, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::Limit<T>& limit, FormatContext& ctx) const -> decltype(ctx.out())
    {
        if (limit.IsUnlimited())
        {
            return formatter<std::wstring_view, wchar_t>::format(L"Unlimited", ctx);
        }

        const auto value = fmt::format(L"{}", limit.value);
        return formatter<std::wstring_view, wchar_t>::format(value, ctx);
    }
};
