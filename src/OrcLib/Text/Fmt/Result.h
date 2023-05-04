//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <fmt/format.h>
#include <boost/outcome/outcome.hpp>
#include "Utils/Result.h"

template <typename T>
struct fmt::formatter<Orc::Result<T>> : public fmt::formatter<T>
{
    template <typename FormatContext>
    auto format(const Orc::Result<T>& result, FormatContext& ctx) -> decltype(ctx.out())
    {
        if (result.has_error())
        {
            fmt::format_to(ctx.out(), "{}", result.error());
            return ctx.out();
        }
        return formatter<T>::format(result.value(), ctx);
    }
};

template <typename T>
struct fmt::formatter<Orc::Result<T>, wchar_t> : public fmt::formatter<T, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::Result<T>& result, FormatContext& ctx) -> decltype(ctx.out())
    {
        if (result.has_error())
        {
            fmt::format_to(ctx.out(), L"{}", result.error());
            return ctx.out();
        }
        return formatter<T, wchar_t>::format(result.value(), ctx);
    }
};
