//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "Configuration/ConfigFile.h"

#include "Configuration/ConfigFile_Common.h"

#pragma managed(push, off)

constexpr auto DD_OUTPUT = 0L;
constexpr auto DD_LOCATIONS = 1L;
constexpr auto DD_LOGGING = 2L;
constexpr auto DD_LOG = 3L;

constexpr auto DD_EMPTYTOOL = 0L;

namespace Orc::Config::DD {
HRESULT root(ConfigItem& parent);
}
