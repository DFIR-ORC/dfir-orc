//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <optional>

#include <fmt/format.h>

template <typename T>
struct fmt::formatter<std::optional<T>> : public fmt::formatter<T>
{
    template <typename FormatContext>
    auto format(const std::optional<T>& optional, FormatContext& ctx) -> decltype(ctx.out())
    {
        if (!optional.has_value())
        {
            formatter<std::string_view> na;
            return na.format("N/A", ctx);
        }

        return formatter<T>::format(optional.value(), ctx);
    }
};

template <typename T>
struct fmt::formatter<std::optional<T>, wchar_t> : public fmt::formatter<T, wchar_t>
{
    template <typename FormatContext>
    auto format(const std::optional<T>& optional, FormatContext& ctx) -> decltype(ctx.out())
    {
        if (!optional.has_value())
        {
            formatter<std::wstring_view, wchar_t> na;
            return na.format(L"N/A", ctx);
        }

        return formatter<T, wchar_t>::format(optional.value(), ctx);
    }
};
