//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string_view>
#include <cwctype>

[[nodiscard]] bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b);

[[nodiscard]] bool StartsWithIgnoreCase(std::wstring_view str, std::wstring_view prefix);
