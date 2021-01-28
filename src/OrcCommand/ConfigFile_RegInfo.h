//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//

#pragma once

#include "OrcLib.h"

#include "Configuration/ConfigFile.h"

#include "Configuration/ConfigFile_Common.h"

#pragma managed(push, off)

constexpr auto REGINFO_OUTPUT = 0L;
constexpr auto REGINFO_HIVE = 1L;
constexpr auto REGINFO_LOGGING = 2L;
constexpr auto REGINFO_LOG = 3L;
constexpr auto REGINFO_INFORMATION = 4L;
constexpr auto REGINFO_COMPUTER = 5L;
constexpr auto REGINFO_LOCATION = 6L;
constexpr auto REGINFO_KNOWNLOCATIONS = 7L;
constexpr auto REGINFO_CSVLIMIT = 8L;

constexpr auto REGINFO_REGINFO = 0L;
constexpr auto REGINFO_TEMPLATE = 0L;

namespace Orc::Config::RegInfo {
ORCLIB_API HRESULT root(ConfigItem& item);
}
#pragma managed(pop)
