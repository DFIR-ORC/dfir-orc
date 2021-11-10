//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <system_error>

#include <fmt/format.h>

#include "Text/Fwd/Iconv.h"

template <>
struct fmt::formatter<std::error_code> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const std::error_code& ec, FormatContext& ctx) -> decltype(ctx.out());
};

template <>
struct fmt::formatter<std::error_code, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const std::error_code& ec, FormatContext& ctx) -> decltype(ctx.out());
};
