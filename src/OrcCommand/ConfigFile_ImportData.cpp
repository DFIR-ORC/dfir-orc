//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

// IMPORTDATA

#include "OrcCommand.h"

#include "ConfigFile_ImportData.h"

using namespace Orc::Config::Common;

ORCUTILS_API HRESULT Orc::Config::ImportData::root(ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    item.strName = L"importdata";
    item.Flags = ConfigItem::MANDATORY;
    item.dwIndex = IMPORTDATA_IMPORTDATA;
    item.Status = ConfigItem::MISSING;

    if (FAILED(hr = item.AddChild(logging, IMPORTDATA_LOGGING)))
        return hr;
    if (FAILED(hr = item.AddChild(L"output", output, IMPORTDATA_OUTPUT)))
        return hr;
    if (FAILED(hr = item.AddChild(L"import_output", output, IMPORTDATA_IMPORT_OUTPUT)))
        return hr;
    if (FAILED(hr = item.AddChild(L"extract_output", output, IMPORTDATA_EXTRACT_OUTPUT)))
        return hr;
    if (FAILED(hr = item.AddChildNodeList(L"input", IMPORTDATA_INPUT, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = item.AddChildNodeList(L"table", IMPORTDATA_TABLE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"recursive", IMPORTDATA_RECURSIVE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"concurrency", IMPORTDATA_CONCURRENCY, ConfigItem::OPTION)))
        return hr;

    auto& input = item[IMPORTDATA_INPUT];
    if (FAILED(hr = input.AddAttribute(L"match", IMPORTDATA_INPUT_MATCH, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = input.AddAttribute(L"directory", IMPORTDATA_INPUT_DIRECTORY, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = input.AddChildNodeList(L"import", IMPORTDATA_INPUT_IMPORT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = input.AddChildNodeList(L"extract", IMPORTDATA_INPUT_EXTRACT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = input.AddChildNodeList(L"ignore", IMPORTDATA_INPUT_IGNORE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = input.AddChildNode(L"before", IMPORTDATA_INPUT_BEFORE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = input.AddChildNode(L"after", IMPORTDATA_INPUT_AFTER, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = input.AddAttribute(L"password", IMPORTDATA_INPUT_PASSWORD, ConfigItem::OPTION)))
        return hr;

    auto& import = input[IMPORTDATA_INPUT_IMPORT];
    if (FAILED(hr = import.AddAttribute(L"filematch", IMPORTDATA_INPUT_IMPORT_FILEMATCH, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = import.AddAttribute(L"table", IMPORTDATA_INPUT_IMPORT_TABLE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = import.AddChildNode(L"before", IMPORTDATA_INPUT_IMPORT_BEFORE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = import.AddChildNode(L"after", IMPORTDATA_INPUT_IMPORT_AFTER, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = import.AddAttribute(L"password", IMPORTDATA_INPUT_IMPORT_PASSWORD, ConfigItem::OPTION)))
        return hr;

    auto& extract = input[IMPORTDATA_INPUT_EXTRACT];
    if (FAILED(hr = extract.AddAttribute(L"filematch", IMPORTDATA_INPUT_EXTRACT_FILEMATCH, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = extract.AddAttribute(L"password", IMPORTDATA_INPUT_EXTRACT_PASSWORD, ConfigItem::OPTION)))
        return hr;

    auto& ignore = input[IMPORTDATA_INPUT_IGNORE];
    if (FAILED(hr = ignore.AddAttribute(L"filematch", IMPORTDATA_INPUT_IGNORE_FILEMATCH, ConfigItem::MANDATORY)))
        return hr;

    auto& table = item[IMPORTDATA_TABLE];
    if (FAILED(hr = table.AddAttribute(L"name", IMPORTDATA_TABLE_NAME, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = table.AddAttribute(L"key", IMPORTDATA_TABLE_KEY, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = table.AddAttribute(L"schema", IMPORTDATA_TABLE_SCHEMA, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = table.AddAttribute(L"disposition", IMPORTDATA_TABLE_DISPOSITION, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = table.AddAttribute(L"compress", IMPORTDATA_TABLE_COMPRESS, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = table.AddAttribute(L"tablock", IMPORTDATA_TABLE_TABLOCK, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = table.AddChildNode(L"before", IMPORTDATA_TABLE_BEFORE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = table.AddChildNode(L"after", IMPORTDATA_TABLE_AFTER, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = table.AddAttribute(L"concurrency", IMPORTDATA_TABLE_CONCURRENCY, ConfigItem::OPTION)))
        return hr;

    return S_OK;
}
