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

constexpr auto IMPORTDATA_IMPORTDATA = 0L;

constexpr auto IMPORTDATA_LOGGING = 0L;
constexpr auto IMPORTDATA_OUTPUT = 1L;
constexpr auto IMPORTDATA_IMPORT_OUTPUT = 2L;
constexpr auto IMPORTDATA_EXTRACT_OUTPUT = 3L;
constexpr auto IMPORTDATA_INPUT = 4L;
constexpr auto IMPORTDATA_TABLE = 5L;
constexpr auto IMPORTDATA_RECURSIVE = 6L;
constexpr auto IMPORTDATA_CONCURRENCY = 7L;

constexpr auto IMPORTDATA_INPUT_MATCH = 0L;
constexpr auto IMPORTDATA_INPUT_DIRECTORY = 1L;
constexpr auto IMPORTDATA_INPUT_IMPORT = 2L;
constexpr auto IMPORTDATA_INPUT_EXTRACT = 3L;
constexpr auto IMPORTDATA_INPUT_IGNORE = 4L;
constexpr auto IMPORTDATA_INPUT_BEFORE = 5L;
constexpr auto IMPORTDATA_INPUT_AFTER = 6L;
constexpr auto IMPORTDATA_INPUT_PASSWORD = 7L;

constexpr auto IMPORTDATA_INPUT_IMPORT_FILEMATCH = 0L;
constexpr auto IMPORTDATA_INPUT_IMPORT_TABLE = 1L;
constexpr auto IMPORTDATA_INPUT_IMPORT_BEFORE = 2L;
constexpr auto IMPORTDATA_INPUT_IMPORT_AFTER = 3L;
constexpr auto IMPORTDATA_INPUT_IMPORT_PASSWORD = 4L;

constexpr auto IMPORTDATA_INPUT_IGNORE_FILEMATCH = 0L;

constexpr auto IMPORTDATA_INPUT_EXTRACT_FILEMATCH = 0L;
constexpr auto IMPORTDATA_INPUT_EXTRACT_PASSWORD = 1L;

constexpr auto IMPORTDATA_TABLE_NAME = 0L;
constexpr auto IMPORTDATA_TABLE_KEY = 1L;
constexpr auto IMPORTDATA_TABLE_SCHEMA = 2L;
constexpr auto IMPORTDATA_TABLE_DISPOSITION = 3L;
constexpr auto IMPORTDATA_TABLE_COMPRESS = 4L;
constexpr auto IMPORTDATA_TABLE_TABLOCK = 5L;
constexpr auto IMPORTDATA_TABLE_BEFORE = 6L;
constexpr auto IMPORTDATA_TABLE_AFTER = 7L;
constexpr auto IMPORTDATA_TABLE_CONCURRENCY = 8L;

namespace Orc::Config::ImportData {
ORCUTILS_API HRESULT root(ConfigItem& item);
}