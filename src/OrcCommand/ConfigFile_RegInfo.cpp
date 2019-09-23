//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//

#include "stdafx.h"

// REGINFO
#include "ConfigFile_RegInfo.h"

using namespace Orc::Config::Common;

HRESULT Orc::Config::RegInfo::root(ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    item.strName = L"reginfo";
    item.Flags = ConfigItem::MANDATORY;
    item.dwIndex = 0L;
    item.Status = ConfigItem::MISSING;

    if (FAILED(hr = item.AddChild(L"output", output, REGINFO_OUTPUT)))
        return hr;
    if (FAILED(hr = item.AddChild(hive, REGINFO_HIVE)))
        return hr;
    if (FAILED(hr = item.AddChild(logging, REGINFO_LOGGING)))
        return hr;
    if (FAILED(hr = item.AddChildNode(L"information", REGINFO_INFORMATION, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"computer", REGINFO_COMPUTER, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddChild(optional_location, REGINFO_LOCATION)))
        return hr;
    if (FAILED(hr = item.AddChild(knownlocations, REGINFO_KNOWNLOCATIONS)))
        return hr;
    if (FAILED(hr = item.AddChildNode(L"csv_limit", REGINFO_CSVLIMIT, ConfigItem::OPTION)))
        return hr;

    return S_OK;
}
