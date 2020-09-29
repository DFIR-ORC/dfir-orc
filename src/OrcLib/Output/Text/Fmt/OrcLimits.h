//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <fmt/format.h>

#include <OrcLimits.h>

#include "Output/Text/Fmt/ByteQuantity.h"
#include "Utils/TypeTraits.h"

template <typename T>
struct fmt::formatter<Orc::Limits::Limit<T>> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::Limits::Limit<T>& limit, FormatContext& ctx)
    {
        if (limit.IsUnlimited())
        {
            return fmt::format_to(ctx.out(), "Unlimited");
        }

        return fmt::format_to(ctx.out(), L"{}", limit.value);
    }
};

template <typename T>
struct fmt::formatter<Orc::Limits::Limit<T>, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::Limits::Limit<T>& limit, FormatContext& ctx)
    {
        if (limit.IsUnlimited())
        {
            return fmt::format_to(ctx.out(), L"Unlimited");
        }

        return fmt::format_to(ctx.out(), L"{}", limit.value);
    }
};
