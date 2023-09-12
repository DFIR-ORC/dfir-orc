//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <string>

#include <Windows.h>

namespace Orc {

struct FlagsDefinition
{
    DWORD dwFlag;
    const WCHAR* szShortDescr;
    const WCHAR* szLongDescr;
};

std::string FlagsToString(DWORD flags, const FlagsDefinition flagValues[], CHAR separator = '|');

// There are FlagsDefinition structure which are used without being flags. Multiple
// functions where doing this conversion without using any mask but raw value.
std::optional<std::wstring> ExactFlagToString(DWORD dwFlags, const FlagsDefinition FlagValues[]);

};  // namespace Orc
