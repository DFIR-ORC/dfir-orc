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

constexpr auto NTFSINFO_FILEINFO = 0L;
constexpr auto NTFSINFO_ATTRINFO = 1L;
constexpr auto NTFSINFO_I30INFO = 2L;
constexpr auto NTFSINFO_TIMELINE = 3L;
constexpr auto NTFSINFO_SECDESCR = 4L;
constexpr auto NTFSINFO_LOCATIONS = 5L;
constexpr auto NTFSINFO_KNOWNLOCATIONS = 6L;
constexpr auto NTFSINFO_COLUMNS = 7L;
constexpr auto NTFSINFO_LOGGING = 8L;
constexpr auto NTFSINFO_LOG = 9L;
constexpr auto NTFSINFO_WALKER = 10L;
constexpr auto NTFSINFO_RESURRECT = 11L;
constexpr auto NTFSINFO_COMPUTER = 12L;
constexpr auto NTFSINFO_POP_SYS_OBJ = 13L;

namespace Orc::Config::NTFSInfo {
HRESULT root(ConfigItem& item);
}

#pragma managed(pop)
