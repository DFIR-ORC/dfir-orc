//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

// GETTHIS
#include "ConfigFile_GetThis.h"

using namespace Orc::Config::Common;

HRESULT Orc::Config::GetThis::root(ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    item.strName = L"getthis";
    item.Flags = ConfigItem::MANDATORY;
    item.dwIndex = 0L;
    item.Status = ConfigItem::MISSING;

    if (FAILED(hr = item.AddChild(L"output", output, GETTHIS_OUTPUT)))
        return hr;
    if (FAILED(hr = item.AddChild(location, GETTHIS_LOCATION)))
        return hr;
    if (FAILED(hr = item.AddChild(knownlocations, GETTHIS_KNOWNLOCATIONS)))
        return hr;
    if (FAILED(hr = item.AddChild(samples, GETTHIS_SAMPLES)))
        return hr;
    if (FAILED(hr = item.AddChild(logging, GETTHIS_LOGGING)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"flushregistry", GETTHIS_FLUSHREGISTRY, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"nolimits", GETTHIS_NOLIMITS, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"reportall", GETTHIS_REPORTALL, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"hash", GETTHIS_HASH, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"fuzzyhash", GETTHIS_FUZZYHASH, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddChild(yara, GETTHIS_YARA)))
        return hr;
    return S_OK;
}
