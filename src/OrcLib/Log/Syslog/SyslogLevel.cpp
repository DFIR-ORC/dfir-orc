//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "stdafx.h"

#include "Log/Syslog/SyslogLevel.h"

#include <string>

namespace Orc {
namespace Log {

std::wstring_view ToString(SyslogLevel SyslogLevel)
{
    switch (SyslogLevel)
    {
        case SyslogLevel::Emergency:
            return kSyslogLevelEmergency;
        case SyslogLevel::Alert:
            return kSyslogLevelAlert;
        case SyslogLevel::Critical:
            return kSyslogLevelCritical;
        case SyslogLevel::Error:
            return kSyslogLevelError;
        case SyslogLevel::Warning:
            return kSyslogLevelWarning;
        case SyslogLevel::Notice:
            return kSyslogLevelNotice;
        case SyslogLevel::Informational:
            return kSyslogLevelInformational;
        case SyslogLevel::Debug:
            return kSyslogLevelDebug;
        case SyslogLevel::Off:
            return kSyslogLevelOff;
        default:
            return L"unkown";
    }
}

Result<SyslogLevel> ToSyslogLevel(const std::wstring& level)
{
    const std::map<std::wstring_view, SyslogLevel> map = {
        {kSyslogLevelEmergency, SyslogLevel::Emergency},
        {kSyslogLevelAlert, SyslogLevel::Alert},
        {kSyslogLevelCritical, SyslogLevel::Critical},
        {kSyslogLevelError, SyslogLevel::Error},
        {kSyslogLevelWarning, SyslogLevel::Warning},
        {kSyslogLevelNotice, SyslogLevel::Notice},
        {kSyslogLevelInformational, SyslogLevel::Informational},
        {kSyslogLevelDebug, SyslogLevel::Debug},
        {kSyslogLevelOff, SyslogLevel::Off}};

    auto it = map.find(level);
    if (it == std::cend(map))
    {
        return std::errc::invalid_argument;
    }

    return it->second;
}

Log::Level ToLevel(Log::SyslogLevel SyslogLevel)
{
    switch (SyslogLevel)
    {
        case SyslogLevel::Emergency:
        case SyslogLevel::Alert:
        case SyslogLevel::Critical:
            return Level::Critical;
        case SyslogLevel::Error:
            return Level::Error;
        case SyslogLevel::Warning:
            return Level::Warning;
        case SyslogLevel::Notice:
        case SyslogLevel::Informational:
            return Level::Info;
        case SyslogLevel::Debug:
            return Level::Debug;
        case SyslogLevel::Off:
            return Level::Off;
        default:
            assert(0);
            return Level::Error;
    }
}

Log::SyslogLevel ToSyslogLevel(Log::Level level)
{
    switch (level)
    {
        case Level::Trace:
        case Level::Debug:
            return SyslogLevel::Debug;
        case Level::Info:
            return SyslogLevel::Informational;
        case Level::Warning:
            return SyslogLevel::Warning;
        case Level::Error:
            return SyslogLevel::Error;
        case Level::Critical:
            return SyslogLevel::Critical;
        case Level::Off:
            return SyslogLevel::Off;
        default:
            assert(0);
            return SyslogLevel::Error;
    }
}

}  // namespace Log
}  // namespace Orc
