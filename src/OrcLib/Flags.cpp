//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl (ANSSI)
//

#include "stdafx.h"

#include "Flags.h"

#include <fmt/core.h>

#include "Utils/Iconv.h"

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
            const auto description = Utf16ToUtf8(flagValues[index].szShortDescr, ec);
            if (ec)
            {
                fmt::format_to(std::back_inserter(out), Orc::kFailedConversion);
                return out;
            }

            if (bFirst)
            {
                bFirst = false;
                fmt::format_to(std::back_inserter(out), "{}", description);
            }

            fmt::format_to(std::back_inserter(out), "{}{}", separator, description);
        }
    }

    return out;
}

}  // namespace Orc
