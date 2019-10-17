//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "OrcLib.h"

#include "ConfigFile.h"

#include "ConfigFile_Common.h"

#pragma managed(push, off)

constexpr auto GETSAMPLES_GETTHIS_EXENAME = 0L;
constexpr auto GETSAMPLES_GETTHIS_EXERUN = 1L;
constexpr auto GETSAMPLES_GETTHIS_EXERUN32 = 2L;
constexpr auto GETSAMPLES_GETTHIS_EXERUN64 = 3L;
constexpr auto GETSAMPLES_GETTHIS_ARGS = 4L;

constexpr auto GETSAMPLES_OUTPUT = 0L;
constexpr auto GETSAMPLES_SAMPLEINFO = 1L;
constexpr auto GETSAMPLES_TIMELINE = 2L;
constexpr auto GETSAMPLES_GETTHIS_CONFIG = 3L;
constexpr auto GETSAMPLES_GETTHIS = 4L;
constexpr auto GETSAMPLES_TEMPDIR = 5L;
constexpr auto GETSAMPLES_LOCATIONS = 6L;
constexpr auto GETSAMPLES_KNOWNLOCATIONS = 7L;
constexpr auto GETSAMPLES_SAMPLES = 8L;
constexpr auto GETSAMPLES_LOGGING = 9L;
constexpr auto GETSAMPLES_AUTORUNS = 10L;
constexpr auto GETSAMPLES_NOLIMITS = 11L;
constexpr auto GETSAMPLES_NOSIGCHECK = 12L;

namespace Orc::Config::GetSamples {
ORCLIB_API HRESULT root(ConfigItem& item);
}
