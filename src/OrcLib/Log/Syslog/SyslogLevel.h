//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <spdlog/common.h>

#include "Log/Level.h"

namespace Orc {
namespace Log {

constexpr std::wstring_view kSyslogLevelEmergency = L"emergency";
constexpr std::wstring_view kSyslogLevelAlert = L"alert";
constexpr std::wstring_view kSyslogLevelCritical = L"critical";
constexpr std::wstring_view kSyslogLevelError = L"error";
constexpr std::wstring_view kSyslogLevelWarning = L"warning";
constexpr std::wstring_view kSyslogLevelNotice = L"notice";
constexpr std::wstring_view kSyslogLevelInformational = L"informational";
constexpr std::wstring_view kSyslogLevelDebug = L"debug";
constexpr std::wstring_view kSyslogLevelOff = L"off";

enum class SyslogLevel : uint8_t
{
    Emergency = 0,
    Alert,
    Critical,
    Error,
    Warning,
    Notice,
    Informational,
    Debug,
    Off
};

std::wstring_view ToString(Log::SyslogLevel level);
Result<Log::SyslogLevel> ToSyslogLevel(const std::wstring& syslogLevel);

Log::Level ToLevel(Log::SyslogLevel level);
Log::SyslogLevel ToSyslogLevel(Log::Level level);

}  // namespace Log
}  // namespace Orc
