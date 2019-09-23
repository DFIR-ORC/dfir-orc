//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

// TOOLEMBED
#include "ConfigFile_ToolEmbed.h"

using namespace Orc;

// <File name="GetThis.exe"          path="..\Win32\%Config%\GetThis.exe" />
HRESULT Orc::Config::ToolEmbed::file2cab(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"file", dwIndex, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"name", TOOLEMBED_FILE2ARCHIVE_NAME, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"path", TOOLEMBED_FILE2ARCHIVE_PATH, ConfigItem::MANDATORY)))
        return hr;
    return S_OK;
}

// <Cab name="ToolCab">
HRESULT Orc::Config::ToolEmbed::archive(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"archive", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"name", TOOLEMBED_ARCHIVE_NAME, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"format", TOOLEMBED_ARCHIVE_FORMAT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"compression", TOOLEMBED_ARCHIVE_COMPRESSION, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(file2cab, TOOLEMBED_FILE2ARCHIVE)))
        return hr;
    return S_OK;
}

// <File name="getthis_config" path="..\GetThisCmd\GetThisSample.xml" />
HRESULT Orc::Config::ToolEmbed::file(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"file", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"name", TOOLEMBED_FILENAME, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"path", TOOLEMBED_FILEPATH, ConfigItem::MANDATORY)))
        return hr;
    return S_OK;
}

// <Pair name="WOLFLAUNCHER_CONFIG" value="res:#wolf_config" />
HRESULT Orc::Config::ToolEmbed::pair(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"pair", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"name", TOOLEMBED_PAIRNAME, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"value", TOOLEMBED_PAIRVALUE, ConfigItem::MANDATORY)))
        return hr;
    return S_OK;
}

HRESULT run(ConfigItem& parent, DWORD dwIndex, const WCHAR* szEltName)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNode(szEltName, dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"args", TOOLEMBED_RUN_ARGS, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

// <ToolEmbed>
HRESULT Orc::Config::ToolEmbed::root(ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    item.strName = L"toolembed";
    item.Flags = ConfigItem::MANDATORY;
    item.dwIndex = 0L;
    item.Status = ConfigItem::MISSING;

    if (FAILED(hr = item.AddChildNode(L"input", TOOLEMBED_INPUT, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = item.AddChild(L"output", Orc::Config::Common::output, TOOLEMBED_OUTPUT)))
        return hr;
    if (FAILED(hr = item.AddChild(pair, TOOLEMBED_PAIR)))
        return hr;
    if (FAILED(hr = item.AddChild(L"run", run, TOOLEMBED_RUN)))
        return hr;
    if (FAILED(hr = item.AddChild(L"run32", run, TOOLEMBED_RUN32)))
        return hr;
    if (FAILED(hr = item.AddChild(L"run64", run, TOOLEMBED_RUN64)))
        return hr;
    if (FAILED(hr = item.AddChild(file, TOOLEMBED_FILE)))
        return hr;
    if (FAILED(hr = item.AddChild(archive, TOOLEMBED_ARCHIVE)))
        return hr;
    if (FAILED(hr = item.AddChild(Orc::Config::Common::logging, TOOLEMBED_LOGGING)))
        return hr;
    return S_OK;
}
