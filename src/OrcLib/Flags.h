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

};  // namespace Orc
