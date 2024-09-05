//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include <fmt/format.h>

#include "Utils/Time.h"

using namespace Orc::Traits;

namespace Orc {

Result<std::chrono::system_clock::time_point> FromSystemTime(const Traits::TimeUtc<SYSTEMTIME>& st)
{
    return FromSystemTime(st.value);
}

Result<std::chrono::system_clock::time_point> FromSystemTime(const SYSTEMTIME& st)
{
    FILETIME ft;
    if (!SystemTimeToFileTime(&st, &ft))
    {
        return LastWin32Error();
    }

    return FromFileTime(ft);
}

std::chrono::system_clock::time_point FromFileTime(const FILETIME& ft)
{
    const auto time = ToTime(ft);
    return std::chrono::system_clock::from_time_t(time);
}

FILETIME ToFileTime(const std::chrono::system_clock::time_point& tp)
{
    const auto time = std::chrono::system_clock::to_time_t(tp);
    return ToFileTime(time);
}

time_t ToTime(const FILETIME& ft)
{
    // A FILETIME is the number of 100-nanosecond intervals since January 1, 1601
    // A time_t is the number of 1 - second intervals since January 1, 1970
    ULARGE_INTEGER ull;
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;
    return ull.QuadPart / 10000000ULL - 11644473600ULL;
}

FILETIME ToFileTime(const time_t& time)
{
    ULARGE_INTEGER ll;
    ll.QuadPart = UInt32x32To64(time, 10000000) + 116444736000000000;

    FILETIME ft;
    ft.dwLowDateTime = ll.LowPart;
    ft.dwHighDateTime = ll.HighPart;
    return ft;
}

Result<TimeUtc<SYSTEMTIME>> ToSystemTime(const std::chrono::system_clock::time_point& tp)
{
    const auto time = std::chrono::system_clock::to_time_t(tp);
    const auto ft = ToFileTime(time);

    Traits::TimeUtc<SYSTEMTIME> st;
    if (!FileTimeToSystemTime(&ft, &st.value))
    {
        return LastWin32Error();
    }

    return st;
}

std::wstring ToStringIso8601(const Traits::TimeUtc<SYSTEMTIME>& time)
{
    const auto& st = time.value;
    return fmt::format(
        L"{}-{:02}-{:02}T{:02}:{:02}:{:02}Z", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

Result<std::wstring> ToStringIso8601(const std::chrono::system_clock::time_point& tp)
{
    const auto time = ToSystemTime(tp);
    if (time.has_error())
    {
        return time.error();
    }

    return ToStringIso8601(time.value());
}

std::string ToAnsiStringIso8601(const Traits::TimeUtc<SYSTEMTIME>& time)
{
    const auto& st = time.value;
    return fmt::format(
        "{}-{:02}-{:02}T{:02}:{:02}:{:02}Z", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

Result<std::string> ToAnsiStringIso8601(const std::chrono::system_clock::time_point& tp)
{
    const auto time = ToSystemTime(tp);
    if (time.has_error())
    {
        return time.error();
    }

    return ToAnsiStringIso8601(time.value());
}

}  // namespace Orc
