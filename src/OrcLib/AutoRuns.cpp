//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "WideAnsi.h"

#include <regex>
#include <string>
#include <algorithm>

#include "FileStream.h"
#include "MemoryStream.h"

#include "AutoRuns.h"

#include "Log/Log.h"

static const auto AUTORUNS_ITEM = 0L;
static const auto AUTORUNS_ITEM_ITEM = 0L;

static const auto AUTORUNS_LOCATION_ITEM = 0L;
static const auto AUTORUNS_TIME = 1L;
static const auto AUTORUNS_ITEMNAME_ITEM = 2L;
static const auto AUTORUNS_ENABLED_ITEM = 3L;
static const auto AUTORUNS_LAUNCHSTRING_ITEM = 4L;
static const auto AUTORUNS_PROFILE_ITEM = 5L;
static const auto AUTORUNS_DESCRIPTION_ITEM = 6L;
static const auto AUTORUNS_COMPANY_ITEM = 7L;
static const auto AUTORUNS_SIGNER_ITEM = 8L;
static const auto AUTORUNS_VERSION_ITEM = 9L;
static const auto AUTORUNS_IMAGEPATH_ITEM = 10L;
static const auto AUTORUNS_MD5HASH_ITEM = 11L;
static const auto AUTORUNS_SHA1HASH_ITEM = 12L;
static const auto AUTORUNS_SHA256HASH_ITEM = 13L;
static const auto AUTORUNS_PESHA1HASH_ITEM = 14L;
static const auto AUTORUNS_PESHA256HASH_ITEM = 15L;
static const auto AUTORUNS_IMPHASH_ITEM = 16L;

using namespace std;

using namespace Orc;

AutoRuns::AutoRuns()
    : m_Reader()
{
    HRESULT hr = E_FAIL;

    m_AutoRuns = ConfigItem(L"autoruns", AUTORUNS_ITEM, ConfigItem::NODE, ConfigItem::MANDATORY);

    if (FAILED(hr = m_AutoRuns.AddChildNodeList(L"item", AUTORUNS_ITEM_ITEM, ConfigItem::MANDATORY)))
    {
        Log::Warn("Failed to populate child node list (code: {:#x})", hr);
        return;
    }

    ConfigItem& item = m_AutoRuns.SubItems[AUTORUNS_ITEM_ITEM];

    item.AddChildNode(L"location", AUTORUNS_LOCATION_ITEM, ConfigItem::OPTION);
    item.AddChildNode(L"time", AUTORUNS_TIME, ConfigItem::OPTION);
    item.AddChildNode(L"itemname", AUTORUNS_ITEMNAME_ITEM, ConfigItem::OPTION);
    item.AddChildNode(L"enabled", AUTORUNS_ENABLED_ITEM, ConfigItem::OPTION);
    item.AddChildNode(L"launchstring", AUTORUNS_LAUNCHSTRING_ITEM, ConfigItem::OPTION);
    item.AddChildNode(L"profile", AUTORUNS_PROFILE_ITEM, ConfigItem::OPTION);
    item.AddChildNode(L"description", AUTORUNS_DESCRIPTION_ITEM, ConfigItem::OPTION);
    item.AddChildNode(L"company", AUTORUNS_COMPANY_ITEM, ConfigItem::OPTION);
    item.AddChildNode(L"signer", AUTORUNS_SIGNER_ITEM, ConfigItem::OPTION);
    item.AddChildNode(L"version", AUTORUNS_VERSION_ITEM, ConfigItem::OPTION);
    item.AddChildNode(L"imagepath", AUTORUNS_IMAGEPATH_ITEM, ConfigItem::OPTION);
    item.AddChildNode(L"md5hash", AUTORUNS_MD5HASH_ITEM, ConfigItem::OPTION);
    item.AddChildNode(L"sha1hash", AUTORUNS_SHA1HASH_ITEM, ConfigItem::OPTION);
    item.AddChildNode(L"sha256hash", AUTORUNS_SHA256HASH_ITEM, ConfigItem::OPTION);
    item.AddChildNode(L"pesha1hash", AUTORUNS_PESHA1HASH_ITEM, ConfigItem::OPTION);
    item.AddChildNode(L"pesha256hash", AUTORUNS_PESHA256HASH_ITEM, ConfigItem::OPTION);
    item.AddChildNode(L"impHash", AUTORUNS_IMPHASH_ITEM, ConfigItem::OPTION);
}

HRESULT AutoRuns::LoadAutoRunsXml(const WCHAR* szXmlFile)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Reader.ReadConfig(szXmlFile, m_AutoRuns)))
    {
        if (FAILED(hr = m_Reader.ReadConfig(szXmlFile, m_AutoRuns, L"utf-16")))
        {
            return hr;
        }
    }
    if (FAILED(hr = m_Reader.CheckConfig(m_AutoRuns)))
        return hr;

    auto filestream = std::make_shared<FileStream>();
    if (filestream == nullptr)
        return E_OUTOFMEMORY;
    auto memstream = std::make_shared<MemoryStream>();
    if (memstream == nullptr)
        return E_OUTOFMEMORY;

    if (FAILED(hr = filestream->ReadFrom(szXmlFile)))
        return hr;

    ULONGLONG cbWritten = 0LL;
    if (FAILED(hr = filestream->CopyTo(memstream, &cbWritten)))
        return hr;

    filestream->Close();
    filestream = nullptr;

    m_xmlDataStream = memstream;
    memstream = nullptr;

    return S_OK;
}

HRESULT AutoRuns::LoadAutoRunsXml(const CBinaryBuffer& XmlData)
{
    HRESULT hr = E_FAIL;

    auto memstream = std::make_shared<MemoryStream>();

    if (FAILED(hr = memstream->OpenForReadOnly(XmlData.GetData(), XmlData.GetCount())))
    {
        Log::Error(L"Failed to open autoruns memory stream");
        return hr;
    }

    if (FAILED(hr = m_Reader.ReadConfig(memstream, m_AutoRuns)))
    {
        ULONG64 ullPos = 0LL;
        memstream->SetFilePointer(0LL, FILE_BEGIN, &ullPos);

        if (FAILED(hr = m_Reader.ReadConfig(memstream, m_AutoRuns, L"utf-16")))
        {
            return hr;
        }
        memstream->SetFilePointer(0LL, FILE_BEGIN, &ullPos);
        m_xmlDataStream = memstream;
    }
    else
    {
        if (FAILED(hr = m_Reader.CheckConfig(m_AutoRuns)))
            return hr;

        ULONG64 ullPos = 0LL;
        memstream->SetFilePointer(0LL, FILE_BEGIN, &ullPos);
        m_xmlDataStream = memstream;
    }

    return S_OK;
}

HRESULT AutoRuns::GetAutoRuns(AutoRunsVector& autoruns)
{
    static wregex filenotfound(L"(File not found: )(.*)");

    autoruns.clear();
    autoruns.reserve(m_AutoRuns.SubItems[AUTORUNS_ITEM_ITEM].NodeList.size());

    std::for_each(
        begin(m_AutoRuns.SubItems[AUTORUNS_ITEM_ITEM].NodeList),
        end(m_AutoRuns.SubItems[AUTORUNS_ITEM_ITEM].NodeList),
        [&autoruns](const ConfigItem& item) {
            AutoRunItem autorun_item = {
                item.SubItems[AUTORUNS_LOCATION_ITEM].c_str(),
                item.SubItems[AUTORUNS_ITEMNAME_ITEM].c_str(),
                item.SubItems[AUTORUNS_ENABLED_ITEM].c_str(),
                item.SubItems[AUTORUNS_LAUNCHSTRING_ITEM].c_str(),
                item.SubItems[AUTORUNS_DESCRIPTION_ITEM].c_str(),
                item.SubItems[AUTORUNS_COMPANY_ITEM].c_str(),
                item.SubItems[AUTORUNS_SIGNER_ITEM].c_str(),
                item.SubItems[AUTORUNS_VERSION_ITEM].c_str(),
                item.SubItems[AUTORUNS_IMAGEPATH_ITEM].c_str(),
                item.SubItems[AUTORUNS_MD5HASH_ITEM].c_str(),
                item.SubItems[AUTORUNS_SHA1HASH_ITEM].c_str(),
                item.SubItems[AUTORUNS_SHA256HASH_ITEM].c_str(),
                item.SubItems[AUTORUNS_PESHA1HASH_ITEM].c_str(),
                item.SubItems[AUTORUNS_PESHA256HASH_ITEM].c_str(),
                item.SubItems[AUTORUNS_IMPHASH_ITEM].c_str(),
                false};

            if (!wcsncmp(
                    autorun_item.ImagePath,
                    L"File not found: ",
                    min(wcslen(autorun_item.ImagePath), wcslen(L"File not found: "))))
            {
                autorun_item.ImagePath += wcslen(L"File not found: ");
            }
            if (item.SubItems[AUTORUNS_SIGNER_ITEM] && !item.SubItems[AUTORUNS_SIGNER_ITEM].empty())
                if (!_wcsnicmp(item.SubItems[AUTORUNS_SIGNER_ITEM].c_str(), L"(Verified) ", wcslen(L"(Verified) ")))
                    autorun_item.IsVerified = true;

            autoruns.push_back(autorun_item);
        });

    return S_OK;
}

HRESULT AutoRuns::GetAutoRunFiles(std::vector<std::wstring>& Modules)
{
    Modules.reserve(m_AutoRuns.SubItems[AUTORUNS_ITEM_ITEM].NodeList.size());
    std::for_each(
        begin(m_AutoRuns.SubItems[AUTORUNS_ITEM_ITEM].NodeList),
        end(m_AutoRuns.SubItems[AUTORUNS_ITEM_ITEM].NodeList),
        [&Modules](const ConfigItem& item) { Modules.push_back(item.SubItems[AUTORUNS_IMAGEPATH_ITEM]); });

    return S_OK;
}

HRESULT AutoRuns::PrintAutoRuns()
{
    return m_Reader.PrintConfig(m_AutoRuns, 0L);
}

HRESULT AutoRuns::EnumItems(AutoRunsEnumItemCallback pCallBack, LPVOID pContext)
{
    std::for_each(begin(m_AutoRuns.NodeList), end(m_AutoRuns.NodeList), [pCallBack, pContext](const ConfigItem& item) {
        AutoRunItem autorun_item = {
            item.SubItems[AUTORUNS_LOCATION_ITEM].c_str(),
            item.SubItems[AUTORUNS_ITEMNAME_ITEM].c_str(),
            item.SubItems[AUTORUNS_ENABLED_ITEM].c_str(),
            item.SubItems[AUTORUNS_LAUNCHSTRING_ITEM].c_str(),
            item.SubItems[AUTORUNS_DESCRIPTION_ITEM].c_str(),
            item.SubItems[AUTORUNS_COMPANY_ITEM].c_str(),
            item.SubItems[AUTORUNS_SIGNER_ITEM].c_str(),
            item.SubItems[AUTORUNS_VERSION_ITEM].c_str(),
            item.SubItems[AUTORUNS_IMAGEPATH_ITEM].c_str(),
            item.SubItems[AUTORUNS_MD5HASH_ITEM].c_str(),
            item.SubItems[AUTORUNS_SHA1HASH_ITEM].c_str(),
            item.SubItems[AUTORUNS_SHA256HASH_ITEM].c_str(),
            item.SubItems[AUTORUNS_PESHA1HASH_ITEM].c_str(),
            item.SubItems[AUTORUNS_PESHA256HASH_ITEM].c_str(),
            item.SubItems[AUTORUNS_IMPHASH_ITEM].c_str(),
            false};

        if (!wcsncmp(
                autorun_item.ImagePath,
                L"File not found: ",
                min(wcslen(autorun_item.ImagePath), wcslen(L"File not found: "))))
        {
            autorun_item.ImagePath += wcslen(L"File not found: ");
        }
        if (item.SubItems[AUTORUNS_SIGNER_ITEM] && !item.SubItems[AUTORUNS_SIGNER_ITEM].empty())
            if (!_wcsnicmp(item.SubItems[AUTORUNS_SIGNER_ITEM].c_str(), L"(Verified) ", wcslen(L"(Verified) ")))
                autorun_item.IsVerified = true;

        pCallBack(autorun_item, pContext);
    });
    return S_OK;
}

AutoRuns::~AutoRuns(void)
{
    m_Reader.DestroyConfig(m_AutoRuns);
}
