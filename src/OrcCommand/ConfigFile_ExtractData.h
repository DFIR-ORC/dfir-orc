//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "OrcLib.h"
#include "OrcCommand.h"

#include "ConfigFile.h"

#include "ConfigFile_Common.h"

#pragma managed(push, off)

constexpr auto IMPORTDATA_IMPORT_OUTPUT = 2L;
constexpr auto EXTRACTDATA_EXTRACTDATA = 0L;

constexpr auto EXTRACTDATA_LOGGING = 0L;
constexpr auto EXTRACTDATA_OUTPUT = 1L;
constexpr auto EXTRACTDATA_EXTRACT_OUTPUT = 3L;
constexpr auto EXTRACTDATA_INPUT = 4L;
constexpr auto EXTRACTDATA_TABLE = 5L;
constexpr auto EXTRACTDATA_RECURSIVE = 6L;
constexpr auto EXTRACTDATA_CONCURRENCY = 7L;

constexpr auto EXTRACTDATA_INPUT_MATCH = 0L;
constexpr auto EXTRACTDATA_INPUT_DIRECTORY = 1L;
constexpr auto EXTRACTDATA_INPUT_EXTRACT = 2L;
constexpr auto EXTRACTDATA_INPUT_EXTRACT = 3L;
constexpr auto EXTRACTDATA_INPUT_IGNORE = 4L;
constexpr auto EXTRACTDATA_INPUT_BEFORE = 5L;
constexpr auto EXTRACTDATA_INPUT_AFTER = 6L;
constexpr auto EXTRACTDATA_INPUT_PASSWORD = 7L;

constexpr auto EXTRACTDATA_INPUT_EXTRACT_FILEMATCH = 0L;
constexpr auto EXTRACTDATA_INPUT_EXTRACT_TABLE = 1L;
constexpr auto EXTRACTDATA_INPUT_EXTRACT_BEFORE = 2L;
constexpr auto EXTRACTDATA_INPUT_EXTRACT_AFTER = 3L;
constexpr auto EXTRACTDATA_INPUT_EXTRACT_PASSWORD = 4L;

constexpr auto EXTRACTDATA_INPUT_IGNORE_FILEMATCH = 0L;

constexpr auto EXTRACTDATA_INPUT_EXTRACT_FILEMATCH = 0L;
constexpr auto EXTRACTDATA_INPUT_EXTRACT_PASSWORD = 1L;

constexpr auto EXTRACTDATA_TABLE_NAME = 0L;
constexpr auto EXTRACTDATA_TABLE_KEY = 1L;
constexpr auto EXTRACTDATA_TABLE_SCHEMA = 2L;
constexpr auto EXTRACTDATA_TABLE_DISPOSITION = 3L;
constexpr auto EXTRACTDATA_TABLE_COMPRESS = 4L;
constexpr auto EXTRACTDATA_TABLE_TABLOCK = 5L;
constexpr auto EXTRACTDATA_TABLE_BEFORE = 6L;
constexpr auto EXTRACTDATA_TABLE_AFTER = 7L;
constexpr auto EXTRACTDATA_TABLE_CONCURRENCY = 8L;

namespace Orc::Config::ExtractData {
ORCUTILS_API HRESULT root(ConfigItem& item);
}
