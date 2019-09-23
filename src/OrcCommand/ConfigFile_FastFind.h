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

constexpr auto FASTFIND_SERVICE_DESCRIPTION = 0L;
constexpr auto FASTFIND_SERVICE_NAME = 1L;
constexpr auto FASTFIND_SERVICE_TYPE = 2L;
constexpr auto FASTFIND_SERVICE_IMAGE = 3L;

constexpr auto FASTFIND_OBJECT_FIND = 0L;

constexpr auto FASTFIND_OBJECT_FIND_TYPE = 0L;
constexpr auto FASTFIND_OBJECT_FIND_NAME = 1L;
constexpr auto FASTFIND_OBJECT_FIND_NAME_MATCH = 2L;
constexpr auto FASTFIND_OBJECT_FIND_NAME_REGEX = 3L;
constexpr auto FASTFIND_OBJECT_FIND_PATH = 4L;
constexpr auto FASTFIND_OBJECT_FIND_PATH_MATCH = 5L;
constexpr auto FASTFIND_OBJECT_FIND_PATH_REGEX = 6L;

constexpr auto FASTFIND_FILESYSTEM_LOCATIONS = 0L;
constexpr auto FASTFIND_FILESYSTEM_KNOWNLOCATIONS = 1L;
constexpr auto FASTFIND_FILESYSTEM_FILEFIND = 2L;
constexpr auto FASTFIND_FILESYSTEM_EXCLUDE = 3L;
constexpr auto FASTFIND_FILESYSTEM_YARA = 4L;

constexpr auto FASTFIND_REGISTRY_LOCATIONS = 0L;
constexpr auto FASTFIND_REGISTRY_KNOWNLOCATIONS = 1L;
constexpr auto FASTFIND_REGISTRY_HIVE = 2L;

constexpr auto FASTFIND_VERSION = 0L;
constexpr auto FASTFIND_LOGGING = 1L;
constexpr auto FASTFIND_FILESYSTEM = 2L;
constexpr auto FASTFIND_REGISTRY = 3L;
constexpr auto FASTFIND_SERVICE = 4L;
constexpr auto FASTFIND_OBJECT = 5L;

constexpr auto FASTFIND_OUTPUT_FILESYSTEM = 6L;
constexpr auto FASTFIND_OUTPUT_REGISTRY = 7L;
constexpr auto FASTFIND_OUTPUT_OBJECT = 8L;
constexpr auto FASTFIND_OUTPUT_STRUCTURED = 9L;

constexpr auto FASTFIND_FASTFIND = 0L;

namespace Orc::Config::FastFind {

ORCLIB_API HRESULT object(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT service(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT registry(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT filesystem(ConfigItem& parent, DWORD dwIndex);

ORCLIB_API HRESULT root(ConfigItem& item);

}  // namespace Orc::Config::FastFind