//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "ConfigFile.h"

#include "ConfigFile_Common.h"

#pragma managed(push, off)

constexpr auto EMPTYTOOL_OUTPUT = 0L;
constexpr auto EMPTYTOOL_LOCATIONS = 1L;
constexpr auto EMPTYTOOL_LOGGING = 2L;

constexpr auto EMPTYTOOL_EMPTYTOOL = 0L;

namespace Orc::Config::EmptyTool {

ORCLIB_API HRESULT root(ConfigItem& parent);

}
