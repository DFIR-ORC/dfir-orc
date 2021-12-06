//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "Configuration/ConfigFile.h"

#pragma managed(push, off)

constexpr auto ORC_OUTPUT = 0L;
constexpr auto ORC_UPLOAD = 1L;
constexpr auto ORC_DOWNLOAD = 2L;
constexpr auto ORC_TEMP = 3L;
constexpr auto ORC_KEY = 4L;
constexpr auto ORC_ENABLE_KEY = 5L;
constexpr auto ORC_DISABLE_KEY = 6L;
constexpr auto ORC_RECIPIENT = 7L;
constexpr auto ORC_LOGGING = 8L;
constexpr auto ORC_PRIORITY = 9L;
constexpr auto ORC_POWERSTATE = 10L;
constexpr auto ORC_ALTITUDE = 11L;

constexpr auto ORC_ORC = 0L;

namespace Orc::Config::Wolf::Local {
HRESULT root(Orc::ConfigItem& item);
}

#pragma managed(pop)
