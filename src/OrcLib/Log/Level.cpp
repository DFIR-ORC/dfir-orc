//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "Log/Level.h"

#include <string>

namespace Orc {
namespace Log {

std::wstring_view ToString(Level level)
{
    switch (level)
    {
        case Level::Trace:
            return kLevelTrace;
        case Level::Debug:
            return kLevelDebug;
        case Level::Info:
            return kLevelInfo;
        case Level::Warning:
            return kLevelWarning;
        case Level::Error:
            return kLevelError;
        case Level::Critical:
            return kLevelCritical;
        case Level::Off:
            return kLevelOff;
        default:
            return L"unkown";
    }
}

Level ToLevel(const std::wstring& level, std::error_code& ec)
{
    const std::map<std::wstring_view, Level> map = {
        {kLevelTrace, Level::Trace},
        {kLevelDebug, Level::Debug},
        {kLevelInfo, Level::Info},
        {kLevelWarning, Level::Warning},
        {kLevelError, Level::Error},
        {kLevelCritical, Level::Critical},
        {kLevelOff, Level::Off}};

    auto it = map.find(level);
    if (it == std::cend(map))
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return Level::Off;
    }

    return it->second;
}

}  // namespace Log
}  // namespace Orc
