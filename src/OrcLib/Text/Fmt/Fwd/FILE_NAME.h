//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string_view>

#include <fmt/format.h>

struct FILE_NAME;

template <>
struct fmt::formatter<FILE_NAME> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const FILE_NAME& fileName, FormatContext& ctx) -> decltype(ctx.out());
};

template <>
struct fmt::formatter<FILE_NAME, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const FILE_NAME& fileName, FormatContext& ctx) -> decltype(ctx.out());
};
