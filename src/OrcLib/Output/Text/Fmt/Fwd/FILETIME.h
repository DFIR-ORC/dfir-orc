//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string_view>

#include <windows.h>

#include <fmt/format.h>

template <>
struct fmt::formatter<FILETIME> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const FILETIME& ft, FormatContext& ctx) -> decltype(ctx.out());
};

template <>
struct fmt::formatter<FILETIME, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const FILETIME& ft, FormatContext& ctx) -> decltype(ctx.out());
};
