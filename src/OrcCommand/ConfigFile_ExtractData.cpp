//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

// EXTRACTDATA

#include "OrcCommand.h"

#include "ConfigFile_ExtractData.h"

using namespace Orc::Config::Common;

ORCUTILS_API HRESULT Orc::Config::ExtractData::root(ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    item.strName = L"importdata";
    item.Flags = ConfigItem::MANDATORY;
    item.dwIndex = EXTRACTDATA_EXTRACTDATA;
    item.Status = ConfigItem::MISSING;

    if (FAILED(hr = item.AddChild(logging, EXTRACTDATA_LOGGING)))
        return hr;
    if (FAILED(hr = item.AddChild(L"output", output, EXTRACTDATA_OUTPUT)))
        return hr;
    if (FAILED(hr = item.AddChild(L"import_output", output, IMPORTDATA_IMPORT_OUTPUT)))
        return hr;
    if (FAILED(hr = item.AddChild(L"extract_output", output, EXTRACTDATA_EXTRACT_OUTPUT)))
        return hr;
    if (FAILED(hr = item.AddChildNodeList(L"input", EXTRACTDATA_INPUT, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = item.AddChildNodeList(L"table", EXTRACTDATA_TABLE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"recursive", EXTRACTDATA_RECURSIVE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"concurrency", EXTRACTDATA_CONCURRENCY, ConfigItem::OPTION)))
        return hr;

    auto& input = item[EXTRACTDATA_INPUT];
    if (FAILED(hr = input.AddAttribute(L"match", EXTRACTDATA_INPUT_MATCH, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = input.AddAttribute(L"directory", EXTRACTDATA_INPUT_DIRECTORY, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = input.AddChildNodeList(L"import", EXTRACTDATA_INPUT_EXTRACT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = input.AddChildNodeList(L"extract", EXTRACTDATA_INPUT_EXTRACT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = input.AddChildNodeList(L"ignore", EXTRACTDATA_INPUT_IGNORE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = input.AddChildNode(L"before", EXTRACTDATA_INPUT_BEFORE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = input.AddChildNode(L"after", EXTRACTDATA_INPUT_AFTER, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = input.AddAttribute(L"password", EXTRACTDATA_INPUT_PASSWORD, ConfigItem::OPTION)))
        return hr;

    auto& import = input[EXTRACTDATA_INPUT_EXTRACT];
    if (FAILED(hr = import.AddAttribute(L"filematch", EXTRACTDATA_INPUT_EXTRACT_FILEMATCH, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = import.AddAttribute(L"table", EXTRACTDATA_INPUT_EXTRACT_TABLE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = import.AddChildNode(L"before", EXTRACTDATA_INPUT_EXTRACT_BEFORE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = import.AddChildNode(L"after", EXTRACTDATA_INPUT_EXTRACT_AFTER, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = import.AddAttribute(L"password", EXTRACTDATA_INPUT_EXTRACT_PASSWORD, ConfigItem::OPTION)))
        return hr;

    auto& extract = input[EXTRACTDATA_INPUT_EXTRACT];
    if (FAILED(hr = extract.AddAttribute(L"filematch", EXTRACTDATA_INPUT_EXTRACT_FILEMATCH, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = extract.AddAttribute(L"password", EXTRACTDATA_INPUT_EXTRACT_PASSWORD, ConfigItem::OPTION)))
        return hr;

    auto& ignore = input[EXTRACTDATA_INPUT_IGNORE];
    if (FAILED(hr = ignore.AddAttribute(L"filematch", EXTRACTDATA_INPUT_IGNORE_FILEMATCH, ConfigItem::MANDATORY)))
        return hr;

    auto& table = item[EXTRACTDATA_TABLE];
    if (FAILED(hr = table.AddAttribute(L"name", EXTRACTDATA_TABLE_NAME, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = table.AddAttribute(L"key", EXTRACTDATA_TABLE_KEY, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = table.AddAttribute(L"schema", EXTRACTDATA_TABLE_SCHEMA, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = table.AddAttribute(L"disposition", EXTRACTDATA_TABLE_DISPOSITION, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = table.AddAttribute(L"compress", EXTRACTDATA_TABLE_COMPRESS, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = table.AddAttribute(L"tablock", EXTRACTDATA_TABLE_TABLOCK, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = table.AddChildNode(L"before", EXTRACTDATA_TABLE_BEFORE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = table.AddChildNode(L"after", EXTRACTDATA_TABLE_AFTER, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = table.AddAttribute(L"concurrency", EXTRACTDATA_TABLE_CONCURRENCY, ConfigItem::OPTION)))
        return hr;

    return S_OK;
}
