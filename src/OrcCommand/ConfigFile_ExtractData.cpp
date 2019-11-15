//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl
//

#include "stdafx.h"

#include "OrcCommand.h"
#include "ConfigFile_ExtractData.h"

using namespace Orc::Config::Common;

ORCUTILS_API HRESULT Orc::Config::ExtractData::root(ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    item.strName = L"extractdata";
    item.Flags = ConfigItem::MANDATORY;
    item.dwIndex = EXTRACTDATA_EXTRACTDATA;
    item.Status = ConfigItem::MISSING;

    hr = item.AddChild(logging, EXTRACTDATA_LOGGING);
    if (FAILED(hr))
        return hr;

    hr = item.AddChild(L"output", output, EXTRACTDATA_OUTPUT);
    if (FAILED(hr))
        return hr;

    hr = item.AddChild(L"report", output, EXTRACTDATA_REPORT_OUTPUT);
    if (FAILED(hr))
        return hr;

    hr = item.AddChildNodeList(L"input", EXTRACTDATA_INPUT, ConfigItem::MANDATORY);
    if (FAILED(hr))
        return hr;

    hr = item.AddAttribute(L"recursive", EXTRACTDATA_RECURSIVE, ConfigItem::OPTION);
    if (FAILED(hr))
        return hr;

    hr = item.AddAttribute(L"concurrency", EXTRACTDATA_CONCURRENCY, ConfigItem::OPTION);
    if (FAILED(hr))
        return hr;

    auto& input = item[EXTRACTDATA_INPUT];

    hr = input.AddAttribute(L"match", EXTRACTDATA_INPUT_MATCH, ConfigItem::MANDATORY);
    if (FAILED(hr))
        return hr;

    hr = input.AddAttribute(L"directory", EXTRACTDATA_INPUT_DIRECTORY, ConfigItem::OPTION);
    if (FAILED(hr))
        return hr;

    hr = input.AddChildNodeList(L"extract", EXTRACTDATA_INPUT_EXTRACT, ConfigItem::OPTION);
    if (FAILED(hr))
        return hr;

    hr = input.AddChildNodeList(L"ignore", EXTRACTDATA_INPUT_IGNORE, ConfigItem::OPTION);
    if (FAILED(hr))
        return hr;

    hr = input.AddChildNode(L"before", EXTRACTDATA_INPUT_BEFORE, ConfigItem::OPTION);
    if (FAILED(hr))
        return hr;

    hr = input.AddChildNode(L"after", EXTRACTDATA_INPUT_AFTER, ConfigItem::OPTION);
    if (FAILED(hr))
        return hr;

    hr = input.AddAttribute(L"password", EXTRACTDATA_INPUT_PASSWORD, ConfigItem::OPTION);
    if (FAILED(hr))
        return hr;

    auto& extract = input[EXTRACTDATA_INPUT_EXTRACT];

    hr = extract.AddAttribute(L"filematch", EXTRACTDATA_INPUT_EXTRACT_FILEMATCH, ConfigItem::MANDATORY);
    if (FAILED(hr))
        return hr;

    hr = extract.AddAttribute(L"password", EXTRACTDATA_INPUT_EXTRACT_PASSWORD, ConfigItem::OPTION);
    if (FAILED(hr))
        return hr;

    auto& ignore = input[EXTRACTDATA_INPUT_IGNORE];
    hr = ignore.AddAttribute(L"filematch", EXTRACTDATA_INPUT_IGNORE_FILEMATCH, ConfigItem::MANDATORY);
    if (FAILED(hr))
        return hr;

    return S_OK;
}
