//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2024 ANSSI. All Rights Reserved.
//
// Author(s): Copilot (refactor)
//

#pragma once

#include <string>

namespace Orc {

// Enum class for WolfPriority, moved from WolfLauncher.h
enum class WolfPriority {
    Idle,
    Low,
    Normal,
    High
};

std::wstring ToString(WolfPriority value);

} // namespace Orc
