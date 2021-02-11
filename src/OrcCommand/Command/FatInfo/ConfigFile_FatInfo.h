//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#include "OrcLib.h"

#include "Configuration/ConfigFile.h"

#pragma managed(push, off)

constexpr auto FATINFO_OUTPUT = 0L;
constexpr auto FATINFO_LOCATIONS = 1L;
constexpr auto FATINFO_LOGGING = 2L;
constexpr auto FATINFO_LOG = 3L;
constexpr auto FATINFO_COLUMNS = 4L;
constexpr auto FATINFO_RESURRECT = 5L;
constexpr auto FATINFO_COMPUTER = 6L;
constexpr auto FATINFO_POP_SYS_OBJ = 7L;

namespace Orc::Config::FatInfo {
ORCLIB_API HRESULT root(ConfigItem& parent);
}
