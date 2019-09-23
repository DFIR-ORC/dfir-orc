//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

// FASTFIND

#include "ConfigFile_FastFind.h"

using namespace Orc::Config::Common;

HRESULT Orc::Config::FastFind::object(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNode(L"object", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChildNodeList(L"object_find", FASTFIND_OBJECT_FIND, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(
            hr = parent[dwIndex][FASTFIND_OBJECT_FIND].AddAttribute(
                L"type", FASTFIND_OBJECT_FIND_TYPE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent[dwIndex][FASTFIND_OBJECT_FIND].AddAttribute(
                L"name", FASTFIND_OBJECT_FIND_NAME, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent[dwIndex][FASTFIND_OBJECT_FIND].AddAttribute(
                L"name_match", FASTFIND_OBJECT_FIND_NAME_MATCH, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent[dwIndex][FASTFIND_OBJECT_FIND].AddAttribute(
                L"name_regex", FASTFIND_OBJECT_FIND_NAME_REGEX, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent[dwIndex][FASTFIND_OBJECT_FIND].AddAttribute(
                L"path", FASTFIND_OBJECT_FIND_PATH, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent[dwIndex][FASTFIND_OBJECT_FIND].AddAttribute(
                L"path_match", FASTFIND_OBJECT_FIND_PATH_MATCH, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent[dwIndex][FASTFIND_OBJECT_FIND].AddAttribute(
                L"path_regex", FASTFIND_OBJECT_FIND_PATH_REGEX, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::FastFind::service(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNode(L"service", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"description", FASTFIND_SERVICE_DESCRIPTION, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"name", FASTFIND_SERVICE_NAME, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"type", FASTFIND_SERVICE_TYPE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"image", FASTFIND_SERVICE_IMAGE, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::FastFind::registry(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNode(L"registry", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(location, FASTFIND_REGISTRY_LOCATIONS)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(knownlocations, FASTFIND_REGISTRY_KNOWNLOCATIONS)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(hive, FASTFIND_REGISTRY_HIVE)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::FastFind::filesystem(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNode(L"filesystem", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(location, FASTFIND_FILESYSTEM_LOCATIONS)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(knownlocations, FASTFIND_FILESYSTEM_KNOWNLOCATIONS)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(ntfs_find, FASTFIND_FILESYSTEM_FILEFIND)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(ntfs_exclude, FASTFIND_FILESYSTEM_EXCLUDE)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(yara, FASTFIND_FILESYSTEM_YARA)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::FastFind::root(ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    item.strName = L"fastfind";
    item.Flags = ConfigItem::MANDATORY;
    item.dwIndex = 0L;
    item.Status = ConfigItem::MISSING;

    if (FAILED(hr = item.AddAttribute(L"version", FASTFIND_VERSION, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddChild(logging, FASTFIND_LOGGING)))
        return hr;
    if (FAILED(hr = item.AddChild(filesystem, FASTFIND_FILESYSTEM)))
        return hr;
    if (FAILED(hr = item.AddChild(registry, FASTFIND_REGISTRY)))
        return hr;
    if (FAILED(hr = item.AddChild(service, FASTFIND_SERVICE)))
        return hr;
    if (FAILED(hr = item.AddChild(object, FASTFIND_OBJECT)))
        return hr;
    if (FAILED(hr = item.AddChild(L"output_filesystem", output, FASTFIND_OUTPUT_FILESYSTEM)))
        return hr;
    if (FAILED(hr = item.AddChild(L"output_registry", output, FASTFIND_OUTPUT_REGISTRY)))
        return hr;
    if (FAILED(hr = item.AddChild(L"output_object", output, FASTFIND_OUTPUT_OBJECT)))
        return hr;
    if (FAILED(hr = item.AddChild(L"output", output, FASTFIND_OUTPUT_STRUCTURED)))
        return hr;

    return S_OK;
}
