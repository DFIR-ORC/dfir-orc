//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <string_view>

namespace Orc {

bool StartsWith(std::string_view string, std::string_view substring);
bool StartsWith(std::wstring_view string, std::wstring_view substring);

bool EndsWith(std::string_view string, std::string_view substring);
bool EndsWith(std::wstring_view string, std::wstring_view substring);

}  // namespace Orc
