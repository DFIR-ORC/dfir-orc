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
            formatter<std::error_code> error;
            return error.format(result.error(), ctx);
        }

        return formatter<T>::format(result.value(), ctx);
    }
};

template <>
struct fmt::formatter<Orc::Result<void>> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::Result<void>& result, FormatContext& ctx) -> decltype(ctx.out())
    {
        if (result.has_error())
        {
            formatter<std::error_code> error;
            return error.format(result.error(), ctx);
        }

        return formatter<std::string_view>::format("Success", ctx);
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

template <>
struct fmt::formatter<Orc::Result<void>, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::Result<void>& result, FormatContext& ctx) -> decltype(ctx.out())
    {
        if (result.has_error())
        {
            formatter<std::error_code, wchar_t> error;
            return error.format(result.error(), ctx);
        }

        return formatter<std::wstring_view, wchar_t>::format(L"Success", ctx);
    }
};
