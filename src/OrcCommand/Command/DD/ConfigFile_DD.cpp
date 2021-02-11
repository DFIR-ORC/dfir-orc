//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

// EMPTYTOOL

#include "ConfigFile_DD.h"

#include "Log/UtilitiesLoggerConfiguration.h"

using namespace Orc::Config::Common;

HRESULT Orc::Config::DD::root(ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    item.strName = L"dd";
    item.Flags = ConfigItem::MANDATORY;
    item.dwIndex = 0L;
    item.Status = ConfigItem::MISSING;

    if (FAILED(hr = item.AddChild(L"output", output, DD_OUTPUT)))
        return hr;
    if (FAILED(hr = item.AddChild(location, DD_LOCATIONS)))
        return hr;
    if (FAILED(hr = item.AddChild(logging, DD_LOGGING)))
        return hr;
    if (FAILED(hr = item.AddChild(Orc::Command::UtilitiesLoggerConfiguration::Register, DD_LOG)))
        return hr;

    return S_OK;
}
