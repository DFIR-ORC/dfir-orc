//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl
//

#include "ConfigFile.h"
#include "ConfigFile_Common.h"

#pragma managed(push, off)

constexpr auto IMPORTDATA_IMPORT_OUTPUT = 2L;
constexpr auto EXTRACTDATA_EXTRACTDATA = 0L;

constexpr auto EXTRACTDATA_LOGGING = 0L;
constexpr auto EXTRACTDATA_OUTPUT = 1L;
constexpr auto EXTRACTDATA_REPORT_OUTPUT = 2L;
constexpr auto EXTRACTDATA_INPUT = 3L;
constexpr auto EXTRACTDATA_RECURSIVE = 4L;
constexpr auto EXTRACTDATA_CONCURRENCY = 5L;

constexpr auto EXTRACTDATA_INPUT_MATCH = 0L;
constexpr auto EXTRACTDATA_INPUT_DIRECTORY = 1L;
constexpr auto EXTRACTDATA_INPUT_EXTRACT = 2L;
constexpr auto EXTRACTDATA_INPUT_IGNORE = 3L;
constexpr auto EXTRACTDATA_INPUT_BEFORE = 4L;
constexpr auto EXTRACTDATA_INPUT_AFTER = 5L;
constexpr auto EXTRACTDATA_INPUT_PASSWORD = 6L;

constexpr auto EXTRACTDATA_INPUT_IGNORE_FILEMATCH = 0L;

constexpr auto EXTRACTDATA_INPUT_EXTRACT_FILEMATCH = 0L;
constexpr auto EXTRACTDATA_INPUT_EXTRACT_PASSWORD = 1L;

namespace Orc::Config::ExtractData {
ORCUTILS_API HRESULT root(ConfigItem& item);
}
