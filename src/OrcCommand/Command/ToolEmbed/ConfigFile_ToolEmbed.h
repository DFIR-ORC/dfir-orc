//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "OrcLib.h"

#include "Configuration/ConfigFile.h"

#include "Configuration/ConfigFile_Common.h"

#pragma managed(push, off)

constexpr auto TOOLEMBED_FILE2ARCHIVE_NAME = 0L;
constexpr auto TOOLEMBED_FILE2ARCHIVE_PATH = 1L;

constexpr auto TOOLEMBED_ARCHIVE_NAME = 0L;
constexpr auto TOOLEMBED_ARCHIVE_FORMAT = 1L;
constexpr auto TOOLEMBED_ARCHIVE_COMPRESSION = 2L;
constexpr auto TOOLEMBED_FILE2ARCHIVE = 3L;

constexpr auto TOOLEMBED_FILENAME = 0L;
constexpr auto TOOLEMBED_FILEPATH = 1L;

constexpr auto TOOLEMBED_PAIRNAME = 0L;
constexpr auto TOOLEMBED_PAIRVALUE = 1L;

constexpr auto TOOLEMBED_RUN_ARGS = 0L;

constexpr auto TOOLEMBED_INPUT = 0L;
constexpr auto TOOLEMBED_OUTPUT = 1L;
constexpr auto TOOLEMBED_PAIR = 2L;
constexpr auto TOOLEMBED_RUN = 3L;
constexpr auto TOOLEMBED_RUN32 = 4L;
constexpr auto TOOLEMBED_RUN64 = 5L;
constexpr auto TOOLEMBED_FILE = 6L;
constexpr auto TOOLEMBED_ARCHIVE = 7L;
constexpr auto TOOLEMBED_LOGGING = 8L;
constexpr auto TOOLEMBED_LOG = 9L;
// <ToolEmbed>

constexpr auto TOOLEMBED_TOOLEMBED = 0L;

namespace Orc::Config::ToolEmbed {

HRESULT file2cab(ConfigItem& parent, DWORD dwIndex);

// <Cab name="ToolCab">
HRESULT archive(ConfigItem& parent, DWORD dwIndex);

// <File name="getthis_config" path="..\GetThisCmd\GetThisSample.xml" />
HRESULT file(ConfigItem& parent, DWORD dwIndex);

// <Pair name="WOLFLAUNCHER_CONFIG" value="res:#wolf_config" />
HRESULT pair(ConfigItem& parent, DWORD dwIndex);

// <ToolEmbed>
HRESULT root(ConfigItem& item);

}  // namespace Orc::Config::ToolEmbed

#pragma managed(pop)
