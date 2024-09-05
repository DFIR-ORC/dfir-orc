//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "Configuration/ConfigFile.h"

#include "Configuration/ConfigFile_Common.h"

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
constexpr auto FASTFIND_FILESYSTEM_RESURRECT = 5L;

constexpr auto FASTFIND_REGISTRY_LOCATIONS = 0L;
constexpr auto FASTFIND_REGISTRY_KNOWNLOCATIONS = 1L;
constexpr auto FASTFIND_REGISTRY_HIVE = 2L;

constexpr auto FASTFIND_VERSION = 0L;
constexpr auto FASTFIND_LOGGING = 1L;
constexpr auto FASTFIND_LOG = 2L;
constexpr auto FASTFIND_FILESYSTEM = 3L;
constexpr auto FASTFIND_REGISTRY = 4L;
constexpr auto FASTFIND_SERVICE = 5L;
constexpr auto FASTFIND_OBJECT = 6L;

constexpr auto FASTFIND_OUTPUT_FILESYSTEM = 7L;
constexpr auto FASTFIND_OUTPUT_REGISTRY = 8L;
constexpr auto FASTFIND_OUTPUT_OBJECT = 9L;
constexpr auto FASTFIND_OUTPUT_STRUCTURED = 10L;

constexpr auto FASTFIND_FASTFIND = 0L;

namespace Orc::Config::FastFind {

HRESULT object(ConfigItem& parent, DWORD dwIndex);
HRESULT service(ConfigItem& parent, DWORD dwIndex);
HRESULT registry(ConfigItem& parent, DWORD dwIndex);
HRESULT filesystem(ConfigItem& parent, DWORD dwIndex);

HRESULT root(ConfigItem& item);

}  // namespace Orc::Config::FastFind
