//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "OrcLib.h"

#include "Configuration/ConfigFile.h"

#include "Configuration/ConfigFile_Common.h"

#pragma managed(push, off)

constexpr auto USNINFO_OUTPUT = 0L;
constexpr auto USNINFO_LOCATIONS = 1L;
constexpr auto USNINFO_KNOWNLOCATIONS = 2L;
constexpr auto USNINFO_LOGGING = 3L;
constexpr auto USNINFO_LOG = 4L;
constexpr auto USNINFO_COMPACT = 5L;

constexpr auto USNINFO_USNINFO = 0L;

namespace Orc::Config::USNInfo {
HRESULT root(ConfigItem& parent);
}

#pragma managed(pop)
