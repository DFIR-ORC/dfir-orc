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
struct fmt::formatter<Orc::Result<T>> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::Result<T>& result, FormatContext& ctx) -> decltype(ctx.out())
    {
        if (result.has_error())
        {
            fmt::memory_buffer msg;
            fmt::format_to(std::back_inserter(msg), "{}", result.error());
            return fmt::formatter<std::string_view>::format(std::begin(msg), ctx);
        }

        return formatter<std::string_view>::format("Success", ctx);
    }
};

template <typename T>
struct fmt::formatter<Orc::Result<T>, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::Result<T>& result, FormatContext& ctx) -> decltype(ctx.out())
    {
        if (result.has_error())
        {
            fmt::wmemory_buffer msg;
            fmt::format_to(std::back_inserter(msg), L"{}", result.error());
            return fmt::formatter<std::wstring_view, wchar_t>::format(std::begin(msg), ctx);
        }

        return formatter<std::wstring_view, wchar_t>::format(L"Success", ctx);
    }
};
