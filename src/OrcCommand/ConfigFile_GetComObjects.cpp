//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

// GETCOMOBJECTS

#include "ConfigFile_GetComObjects.h"

using namespace Orc::Config::Common;

HRESULT Orc::Config::GetComObjects::root(ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    item.strName = L"getcomobjects";
    item.Flags = ConfigItem::MANDATORY;
    item.dwIndex = 0L;
    item.Status = ConfigItem::MISSING;

    if (FAILED(hr = item.AddChild(L"output", output, GETCOMOBJECTS_OUTPUT)))
        return hr;
    if (FAILED(hr = item.AddChild(hive, GETCOMOBJECTS_HIVE)))
        return hr;
    if (FAILED(hr = item.AddChild(location, GETCOMOBJECTS_LOCATIONS)))
        return hr;
    if (FAILED(hr = item.AddChild(logging, GETCOMOBJECTS_LOGGING)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"computer", GETCOMOBJECTS_COMPUTER, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}
