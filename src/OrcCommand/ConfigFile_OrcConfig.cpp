//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "Configuration/ConfigFile_Common.h"

#include "ConfigFile_OrcConfig.h"
#include "Command/WolfLauncher/ConfigFile_WOLFLauncher.h"
#include "Command/WolfLauncher/ConsoleConfiguration.h"
#include "Log/UtilitiesLoggerConfiguration.h"

HRESULT Orc::Config::Wolf::Local::root(ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    item.strName = L"dfir-orc";
    item.Flags = ConfigItem::MANDATORY;
    item.dwIndex = 0L;
    item.Status = ConfigItem::MISSING;

    if (FAILED(hr = item.AddChild(L"output", Orc::Config::Common::output, ORC_OUTPUT)))
        return hr;
    if (FAILED(hr = Orc::Config::Wolf::upload(item, ORC_UPLOAD)))
        return hr;
    if (FAILED(hr = item.AddChild(L"download", Orc::Config::Common::download, ORC_DOWNLOAD)))
        return hr;
    if (FAILED(hr = item.AddChild(L"temporary", Orc::Config::Common::output, ORC_TEMP)))
        return hr;
    if (FAILED(hr = item.AddChildNodeList(L"key", ORC_KEY, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddChildNodeList(L"enable_key", ORC_ENABLE_KEY, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddChildNodeList(L"disable_key", ORC_DISABLE_KEY, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddChild(Orc::Config::Wolf::recipient, ORC_RECIPIENT)))
        return hr;
    if (FAILED(hr = item.AddChild(Orc::Command::UtilitiesLoggerConfiguration::Register, ORC_LOGGING)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"priority", ORC_PRIORITY, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"powerstate", ORC_POWERSTATE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"altitude", ORC_ALTITUDE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddChild(Orc::Command::ConsoleConfiguration::Register, ORC_CONSOLE)))
        return hr;
    // Same indices as on the embedded <log>/<console> nodes: both are built by the same Register functions.
    // There is no local <outline> or <outcome>: their path, encryption and upload stay with the embedded
    // configuration.
    if (FAILED(hr = item[ORC_LOGGING].AddAttribute(L"encrypt", WOLFLAUNCHER_LOG_ENCRYPT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item[ORC_LOGGING].AddAttribute(L"upload", WOLFLAUNCHER_LOG_UPLOAD, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item[ORC_CONSOLE].AddAttribute(L"encrypt", WOLFLAUNCHER_CONSOLE_ENCRYPT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item[ORC_CONSOLE].AddAttribute(L"upload", WOLFLAUNCHER_CONSOLE_UPLOAD, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"norelocate", ORC_NORELOCATE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"MultipleInstance", ORC_MULTIPLE_INSTANCE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"InstanceId", ORC_INSTANCE_ID, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}
