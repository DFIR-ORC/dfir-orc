//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <array>
#include <string>

#include <windows.h>

#include "Utils/TypeTraits.h"
#include "Text/Fmt/SYSTEMTIME.h"

namespace Orc {
namespace Text {

inline auto FormatSystemTimeUtcA(const SYSTEMTIME& st)
{
    return fmt::format(
        "{}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond,
        st.wMilliseconds);
}

inline auto FormatSystemTimeUtcW(const SYSTEMTIME& st)
{
    return fmt::format(
        L"{}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",
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

template <typename T>
struct fmt::formatter<Orc::Traits::TimeUtc<T>> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::Traits::TimeUtc<T>& time, FormatContext& ctx) const -> decltype(ctx.out())
    {
        std::string s = Orc::Text::FormatSystemTimeUtcA(time);
        return formatter<std::string_view>::format(s, ctx);
    }
};

template <typename T>
struct fmt::formatter<Orc::Traits::TimeUtc<T>, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::Traits::TimeUtc<T>& time, FormatContext& ctx) const -> decltype(ctx.out())
    {
        std::wstring s = Orc::Text::FormatSystemTimeUtcW(time);
        return formatter<std::wstring_view, wchar_t>::format(s, ctx);
    }
};
