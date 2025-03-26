//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <windows.h>

namespace Orc {
namespace Text {

inline auto FormatSystemTimeA(const SYSTEMTIME& st)
{
    return fmt::format(
        "{}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond,
        st.wMilliseconds);
}

inline auto FormatSystemTimeW(const SYSTEMTIME& st)
{
    return fmt::format(
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
struct fmt::formatter<SYSTEMTIME> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const SYSTEMTIME& st, FormatContext& ctx) const -> decltype(ctx.out())
    {
        return formatter<std::string_view>::format(Orc::Text::FormatSystemTimeA(st), ctx);
    }
};

template <>
struct fmt::formatter<SYSTEMTIME, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const SYSTEMTIME& st, FormatContext& ctx) const -> decltype(ctx.out())
    {
        return formatter<std::wstring_view, wchar_t>::format(Orc::Text::FormatSystemTimeW(st), ctx);
    }
};
