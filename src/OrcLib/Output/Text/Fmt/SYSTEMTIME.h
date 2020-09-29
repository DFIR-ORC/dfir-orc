//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <windows.h>

#include <fmt/format.h>

namespace Orc {
namespace Text {

template <typename FormatContext>
auto FormatSystemTimeA(const SYSTEMTIME& st, FormatContext& ctx)
{
    return fmt::format_to(
        ctx.out(),
        "{}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond,
        st.wMilliseconds);
}

template <typename FormatContext>
auto FormatSystemTimeW(const SYSTEMTIME& st, FormatContext& ctx)
{
    return fmt::format_to(
        ctx.out(),
        L"{}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond,
        st.wMilliseconds);
}

}  // namespace Text
}  // namespace Orc

template <>
struct fmt::formatter<SYSTEMTIME> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const SYSTEMTIME& st, FormatContext& ctx)
    {
        return Orc::Text::FormatSystemTimeA(st, ctx);
    }
};

template <>
struct fmt::formatter<SYSTEMTIME, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const SYSTEMTIME& st, FormatContext& ctx)
    {
        return Orc::Text::FormatSystemTimeW(st, ctx);
    }
};
