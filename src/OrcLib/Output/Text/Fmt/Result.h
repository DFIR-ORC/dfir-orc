//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Output/Text/Fmt/Fwd/Result.h"

#include "Utils/Result.h"

template <typename T>
template <typename FormatContext>
auto fmt::formatter<Orc::Result<T>>::format(const Orc::Result<T>& result, FormatContext& ctx) -> decltype(ctx.out())
{
    if (result.has_error())
    {
        const auto msg = fmt::format("{}", result.error());
        return fmt::formatter<std::string_view>::format(msg, ctx);
    }

    return formatter<std::string_view>::format("Success", ctx);
}

template <typename T>
template <typename FormatContext>
auto fmt::formatter<Orc::Result<T>, wchar_t>::format(const Orc::Result<T>& result, FormatContext& ctx)
    -> decltype(ctx.out())
{
    if (result.has_error())
    {
        const auto msg = fmt::format(L"{}", result.error());
        return fmt::formatter<std::wstring_view, wchar_t>::format(msg, ctx);
    }

    return formatter<std::wstring_view, wchar_t>::format(L"Success", ctx);
}
