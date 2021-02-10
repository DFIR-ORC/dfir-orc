//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

// USNINFO

#include "ConfigFile_USNInfo.h"

#include "Log/UtilitiesLoggerConfiguration.h"

HRESULT Orc::Config::USNInfo::root(ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    item.strName = L"usninfo";
    item.Flags = ConfigItem::MANDATORY;
    item.dwIndex = 0L;
    item.Status = ConfigItem::MISSING;

    if (FAILED(hr = item.AddChild(L"output", Orc::Config::Common::output, USNINFO_OUTPUT)))
        return hr;
    if (FAILED(hr = item.AddChild(Orc::Config::Common::location, USNINFO_LOCATIONS)))
        return hr;
    if (FAILED(hr = item.AddChild(Orc::Config::Common::knownlocations, USNINFO_KNOWNLOCATIONS)))
        return hr;
    if (FAILED(hr = item.AddChild(Orc::Config::Common::logging, USNINFO_LOGGING)))
        return hr;
    if (FAILED(hr = item.AddChild(Orc::Command::UtilitiesLoggerConfiguration::Register, USNINFO_LOG)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"compact", USNINFO_COMPACT, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}
