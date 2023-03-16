//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2023 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/in_addr.h"

namespace fmt_in_addr {

static const char* kNotApplicable = "<N/A>";
static const wchar_t* kNotApplicableW = L"<N/A>";

}  // namespace fmt_in_addr

template <>
struct fmt::formatter<in_addr> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const in_addr& ip, FormatContext& ctx) -> decltype(ctx.out())
    {
        auto s = Orc::ToString(ip).value_or(fmt_in_addr::kNotApplicable);
        return formatter<std::string_view>::format(s, ctx);
    }
};

template <>
struct fmt::formatter<in_addr, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const in_addr& ip, FormatContext& ctx) -> decltype(ctx.out())
    {
        auto s = Orc::ToWString(ip).value_or(fmt_in_addr::kNotApplicableW);
        return formatter<std::wstring_view, wchar_t>::format(s, ctx);
    }
};

template <>
struct fmt::formatter<in6_addr> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const in6_addr& ip, FormatContext& ctx) -> decltype(ctx.out())
    {
        auto s = Orc::ToString(ip).value_or(fmt_in_addr::kNotApplicable);
        return formatter<std::string_view>::format(s, ctx);
    }
};

template <>
struct fmt::formatter<in6_addr, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const in6_addr& ip, FormatContext& ctx) -> decltype(ctx.out())
    {
        auto s = Orc::ToWString(ip).value_or(fmt_in_addr::kNotApplicableW);
        return formatter<std::wstring_view, wchar_t>::format(s, ctx);
    }
};
