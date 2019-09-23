//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "UtilitiesMain.h"

#include "LogFileWriter.h"
#include "ParameterCheck.h"
#include "ConfigFileReader.h"
#include "ConfigFile.h"
#include "SystemDetails.h"

#include "EmbeddedResource.h"

using namespace Orc;
using namespace Orc::Command;

HRESULT UtilitiesMain::ReadConfiguration(
    int argc,
    LPCWSTR argv[],
    LPCWSTR szCmdLineOption,
    LPCWSTR szResourceID,
    LPCWSTR szRefResourceID,
    LPCWSTR szConfigExt,
    ConfigItem& configitem,
    ConfigItem::InitFunction init)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = init(configitem)))
    {
        log::Error(_L_, hr, L"Failed to initialize config item schema\r\n");
        return hr;
    }

    ConfigFileReader r(_L_);
    if (FAILED(
            hr = ConfigFile::LookupAndReadConfiguration(
                _L_, argc, argv, r, szCmdLineOption, szResourceID, szRefResourceID, szConfigExt, configitem)))
    {
        log::Error(_L_, hr, L"Failed to lookup and read item schema\r\n");
        return hr;
    }

    return S_OK;
}
