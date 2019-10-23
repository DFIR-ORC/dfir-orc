//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcCommand.h"
#include "ConfigItem.h"

#include "FSUtils.h"

#pragma managed(push, off)

constexpr auto HASVERSIONINFO = 0L;
constexpr auto HASPE = 1L;
constexpr auto SIZELT = 2L;
constexpr auto SIZEGT = 3L;
constexpr auto EXTBINARY = 4L;
constexpr auto EXTARCHIVE = 5L;
constexpr auto EXT = 6L;

constexpr auto DEFAULT = 0L;
constexpr auto ADD = 1L;
constexpr auto OMIT = 2L;
constexpr auto WRITERRORS = 3L;

namespace Orc::Command {

class ORCUTILS_API FileInfoCommon
{
public:
    // filter related static methods
    static WCHAR** GetFilterExtCustomFromString(LPCWSTR szExtCustom);
    static HRESULT GetFilterFromConfig(
        const logger& pLog,
        const ConfigItem& config,
        Filter& filter,
        const ColumnNameDef aliasNames[],
        const ColumnNameDef columnNames[]);
    static HRESULT GetFilterFromArg(
        const logger& pLog,
        LPCWSTR szConstArg,
        Filter& filter,
        const ColumnNameDef aliasNames[],
        const ColumnNameDef columnNames[]);
    static HRESULT GetFiltersFromArgcArgv(
        const logger& pLog,
        int argc,
        LPCWSTR argv[],
        std::vector<Filter>& filters,
        const ColumnNameDef aliasNames[],
        const ColumnNameDef columnNames[]);
    static void FreeFilters(Filter* pFilters);

    // file info config related static methods
    static HRESULT ConfigItem_fileinfo_filter(ConfigItem& parent, LPWSTR szName, DWORD dwIndex);
    static HRESULT ConfigItem_fileinfo_add_filter(ConfigItem& parent, DWORD dwIndex);
    static HRESULT ConfigItem_fileinfo_omit_filter(ConfigItem& parent, DWORD dwIndex);
    static HRESULT ConfigItem_fileinfo_column(ConfigItem& parent, DWORD dwIndex);
};
}  // namespace Orc::Command

#pragma managed(pop)
