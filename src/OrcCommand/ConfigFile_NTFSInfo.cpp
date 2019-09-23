//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "ConfigFile_NTFSInfo.h"
#include "ConfigFile_Common.h"
#include "FileInfoCommon.h"

using namespace Orc::Config::Common;
using namespace Orc::Command;

HRESULT Orc::Config::NTFSInfo::root(ConfigItem& item)
{
    HRESULT hr = E_FAIL;
    item.strName = L"ntfsinfo";
    item.Flags = ConfigItem::MANDATORY;
    item.dwIndex = 0L;
    item.Status = ConfigItem::MISSING;

    if (FAILED(hr = item.AddChild(L"fileinfo", output, NTFSINFO_FILEINFO)))
        return hr;
    if (FAILED(hr = item.AddChild(L"attrinfo", output, NTFSINFO_ATTRINFO)))
        return hr;
    if (FAILED(hr = item.AddChild(L"i30info", output, NTFSINFO_I30INFO)))
        return hr;
    if (FAILED(hr = item.AddChild(L"timeline", output, NTFSINFO_TIMELINE)))
        return hr;
    if (FAILED(hr = item.AddChild(L"secdescr", output, NTFSINFO_SECDESCR)))
        return hr;
    if (FAILED(hr = item.AddChild(location, NTFSINFO_LOCATIONS)))
        return hr;
    if (FAILED(hr = item.AddChild(knownlocations, NTFSINFO_KNOWNLOCATIONS)))
        return hr;
    if (FAILED(hr = item.AddChild(FileInfoCommon::ConfigItem_fileinfo_column, NTFSINFO_COLUMNS)))
        return hr;
    if (FAILED(hr = item.AddChild(logging, NTFSINFO_LOGGING)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"walker", NTFSINFO_WALKER, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"resurrect", NTFSINFO_RESURRECT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"computer", NTFSINFO_COMPUTER, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"popsysobj", NTFSINFO_POP_SYS_OBJ, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}
