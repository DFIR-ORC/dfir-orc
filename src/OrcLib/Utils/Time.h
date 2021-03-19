//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <chrono>
#include <time.h>

#include <windows.h>

#include "TypeTraits.h"

namespace Orc {

Result<std::chrono::system_clock::time_point> FromSystemTime(const SYSTEMTIME& st);
Result<std::chrono::system_clock::time_point> FromSystemTime(const Traits::TimeUtc<SYSTEMTIME>& st);
std::chrono::system_clock::time_point FromFileTime(const FILETIME& ft);

FILETIME ToFileTime(const std::chrono::system_clock::time_point& tp);
FILETIME ToFileTime(const time_t& time);

Result<Traits::TimeUtc<SYSTEMTIME>> ToSystemTime(const std::chrono::system_clock::time_point& tp);

time_t ToTime(const FILETIME& ft);

std::wstring ToStringIso8601(const Traits::TimeUtc<SYSTEMTIME>& time);
Result<std::wstring> ToStringIso8601(const std::chrono::system_clock::time_point& tp);

std::string ToAnsiStringIso8601(const Traits::TimeUtc<SYSTEMTIME>& time);
Result<std::string> ToAnsiStringIso8601(const std::chrono::system_clock::time_point& tp);

}  // namespace Orc
