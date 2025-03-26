//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#include "stdafx.h"

#include "ConfigFile_FatInfo.h"
#include "Configuration/ConfigFile_Common.h"
#include "FileInfoCommon.h"

#include "Log/UtilitiesLoggerConfiguration.h"

using namespace Orc::Config::Common;
using namespace Orc::Command;

HRESULT Orc::Config::FatInfo::root(ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    item.strName = L"fatinfo";
    item.Flags = ConfigItem::MANDATORY;
    item.dwIndex = 0L;
    item.Status = ConfigItem::MISSING;

    if (FAILED(hr = item.AddChild(L"output", output, FATINFO_OUTPUT)))
        return hr;
    if (FAILED(hr = item.AddChild(location, FATINFO_LOCATIONS)))
        return hr;
    if (FAILED(hr = item.AddChild(logging, FATINFO_LOGGING)))
        return hr;
    if (FAILED(hr = item.AddChild(Orc::Command::UtilitiesLoggerConfiguration::Register, FATINFO_LOG)))
        return hr;
    if (FAILED(hr = item.AddChild(FileInfoCommon::ConfigItem_fileinfo_column, FATINFO_COLUMNS)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"resurrect", FATINFO_RESURRECT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"computer", FATINFO_COMPUTER, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"popsysobj", FATINFO_POP_SYS_OBJ, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}
