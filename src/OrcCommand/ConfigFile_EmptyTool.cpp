//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

// EMPTYTOOL

#include "ConfigFile_EmptyTool.h"

using namespace Orc::Config::Common;

HRESULT Orc::Config::EmptyTool::root(ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    item.strName = L"emptytool";
    item.Flags = ConfigItem::MANDATORY;
    item.dwIndex = 0L;
    item.Status = ConfigItem::MISSING;

    if (FAILED(hr = item.AddChild(L"output", output, EMPTYTOOL_OUTPUT)))
        return hr;
    if (FAILED(hr = item.AddChild(location, EMPTYTOOL_LOCATIONS)))
        return hr;
    if (FAILED(hr = item.AddChild(logging, EMPTYTOOL_LOGGING)))
        return hr;
    return S_OK;
}
