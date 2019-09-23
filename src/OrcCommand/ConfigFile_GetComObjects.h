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

constexpr auto GETCOMOBJECTS_OUTPUT = 0L;
constexpr auto GETCOMOBJECTS_HIVE = 1L;
constexpr auto GETCOMOBJECTS_LOCATIONS = 2L;
constexpr auto GETCOMOBJECTS_LOGGING = 3L;
constexpr auto GETCOMOBJECTS_COMPUTER = 4L;

constexpr auto GETCOMOBJECTS_GETCOMOBJECTS = 0L;

namespace Orc::Config::GetComObjects {
ORCLIB_API HRESULT root(ConfigItem& parent);
}
