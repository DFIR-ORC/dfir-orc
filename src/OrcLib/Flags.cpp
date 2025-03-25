//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl (ANSSI)
//

#include "stdafx.h"

#include "Flags.h"

#include <fmt/core.h>

#include "Text/Iconv.h"

namespace Orc {

std::string FlagsToString(DWORD flags, const FlagsDefinition flagValues[], CHAR separator)
{
    std::string out;

    bool bFirst = true;
    for (size_t index = 0; flagValues[index].dwFlag != 0xFFFFFFFF; ++index)
    {
        if (flags & flagValues[index].dwFlag)
        {
            std::error_code ec;
            const auto description = ToUtf8(flagValues[index].szShortDescr, ec);
            if (ec)
            {
                fmt::format_to(std::back_inserter(out), Orc::kFailedConversion);
                return out;
            }

            if (bFirst)
            {
                bFirst = false;
                fmt::format_to(std::back_inserter(out), "{}", description);
                continue;
            }

            fmt::format_to(std::back_inserter(out), "{}{}", separator, description);
        }
    }

    return out;
}

std::wstring FlagsToStringW(DWORD flags, const FlagsDefinition flagValues[], WCHAR separator)
{
    std::wstring out;

    bool bFirst = true;
    for (size_t index = 0; flagValues[index].dwFlag != 0xFFFFFFFF; ++index)
    {
        if (flags & flagValues[index].dwFlag)
        {
            if (bFirst)
            {
                bFirst = false;
                fmt::format_to(std::back_inserter(out), L"{}", flagValues[index].szShortDescr);
                continue;
            }

            fmt::format_to(std::back_inserter(out), L"{}{}", separator, flagValues[index].szShortDescr);
        }
    }

    return out;
}

std::optional<std::wstring> ExactFlagToString(DWORD dwFlags, const FlagsDefinition FlagValues[])
{
    int idx = 0;
    bool found = false;

    std::wstring buffer;
    while (FlagValues[idx].dwFlag != 0xFFFFFFFF)
    {
        if (dwFlags == FlagValues[idx].dwFlag)
        {
            fmt::format_to(std::back_inserter(buffer), L"{}", FlagValues[idx].szShortDescr);
            found = true;
            break;
        }
        idx++;
    }

    if (!found)
    {
        return {};
    }

    return buffer;
}

}  // namespace Orc
