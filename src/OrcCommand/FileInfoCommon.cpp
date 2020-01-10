//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "ParameterCheck.h"
#include "FileInfoCommon.h"
#include "FileInfo.h"

#include "LogFileWriter.h"

using namespace Orc;
using namespace Orc::Command;

WCHAR** FileInfoCommon::GetFilterExtCustomFromString(LPCWSTR szExtCustom)
{
    const WCHAR* pCur = szExtCustom;
    DWORD dwNumExt = 0L;
    WCHAR** ppExtCustom = NULL;

    while (pCur)
    {
        const WCHAR* pNext = wcschr(pCur, L',');
        if (pNext)
        {
            dwNumExt++;
            pCur = pNext + 1;
        }
        else
        {
            dwNumExt++;
            pCur = NULL;
        }
    }

    if (dwNumExt)
    {
        ppExtCustom = new WCHAR*[msl::utilities::SafeInt<USHORT>(dwNumExt) + 1];
        memset(ppExtCustom, 0L, sizeof(WCHAR*) * (dwNumExt + 1));

        pCur = szExtCustom;
        dwNumExt = 0L;

        while (pCur)
        {
            const WCHAR* pNext = wcschr(pCur, L',');
            if (pNext)
            {
                *const_cast<WCHAR*>(pNext) = L'\0';
                ppExtCustom[dwNumExt] = _wcsdup(pCur);
                if (ppExtCustom[dwNumExt] != nullptr)
                {
                    for (WCHAR* pExtChar = ppExtCustom[dwNumExt]; *pExtChar; pExtChar++)
                        *pExtChar = towupper(*pExtChar);
                    pCur = pNext + 1;
                }
                dwNumExt++;
            }
            else
            {
                ppExtCustom[dwNumExt] = _wcsdup(pCur);

                if (ppExtCustom[dwNumExt] != nullptr)
                {
                    for (WCHAR* pExtChar = ppExtCustom[dwNumExt]; *pExtChar; pExtChar++)
                        *pExtChar = towupper(*pExtChar);
                }
                dwNumExt++;
                pCur = NULL;
            }
        }
    }

    return ppExtCustom;
}

HRESULT FileInfoCommon::GetFilterFromConfig(
    const logger& pLog,
    const ConfigItem& config,
    Filter& filter,
    const ColumnNameDef aliasNames[],
    const ColumnNameDef columnNames[])
{
    HRESULT hr = E_FAIL;
    bool bDefined = false;

    if (config)
    {
        filter.intent = FileInfo::GetIntentions(pLog, config.c_str(), aliasNames, columnNames);
        if (filter.intent == FILEINFO_NONE)
        {
            log::Error(pLog, E_INVALIDARG, L"Column specified (%s) is invalid\r\n", config.c_str());
            return E_INVALIDARG;
        }
    }
    else
    {
        log::Error(pLog, E_INVALIDARG, L"Column filter specified with no column definition\r\n");
        return E_INVALIDARG;
    }

    if (config[HASVERSIONINFO])
    {
        filter.type = FILEFILTER_VERSIONINFO;
        bDefined = true;
    }
    if (config[HASPE])
    {
        filter.type = FILEFILTER_PEHEADER;
        if (bDefined)
        {
            log::Error(pLog, E_INVALIDARG, L"Ignored criteria HasPE, critera already defined\r\n");
        }
        else
            bDefined = true;
    }
    if (config[EXTBINARY])
    {
        filter.type = FILEFILTER_EXTBINARY;
        if (bDefined)
        {
            log::Error(pLog, E_INVALIDARG, L"Ignored criteria ExtBinary, critera already defined\r\n");
        }
        else
            bDefined = true;
    }
    if (config[EXTARCHIVE])
    {
        filter.type = FILEFILTER_EXTARCHIVE;
        if (bDefined)
        {
            log::Error(pLog, E_INVALIDARG, L"Ignored criteria ExtArchive, critera already defined\r\n");
        }
        else
            bDefined = true;
    }
    if (config[EXT])
    {
        filter.type = FILEFILTER_EXTCUSTOM;
        if (bDefined)
        {
            log::Error(pLog, E_INVALIDARG, L"Ignored criteria Ext, critera already defined\r\n");
        }
        else
            bDefined = true;

        filter.filterdata.extcustom = GetFilterExtCustomFromString(config[EXT].c_str());
    }
    if (config[SIZEGT])
    {
        filter.type = FILEFILTER_SIZEMORE;
        if (bDefined)
        {
            log::Error(pLog, E_INVALIDARG, L"Ignored criteria SizeGT, critera already defined\r\n");
        }
        else
            bDefined = true;

        if (FAILED(hr = GetFileSizeFromArg(config[SIZEGT].c_str(), filter.filterdata.size)))
            return hr;
    }
    if (config[SIZELT])
    {
        filter.type = FILEFILTER_SIZELESS;
        if (bDefined)
        {
            log::Error(pLog, E_INVALIDARG, L"Ignored criteria SizeLT, critera already defined\r\n");
        }
        else
            bDefined = true;

        if (FAILED(hr = GetFileSizeFromArg(config[SIZELT].c_str(), filter.filterdata.size)))
            return hr;
    }

    if (!bDefined)
        return S_FALSE;
    else
        return S_OK;
}

HRESULT FileInfoCommon::GetFiltersFromArgcArgv(
    const logger& pLog,
    int argc,
    LPCWSTR argv[],
    std::vector<Filter>& filters,
    const ColumnNameDef aliasNames[],
    const ColumnNameDef columnNames[])
{
    HRESULT hr = E_FAIL;
    DWORD dwFiltNum = 0L;
    for (int i = 0; i < argc; i++)
        if ((argv[i][0] == L'/' || argv[i][0] == L'-') && (argv[i][1] == L'+' || argv[i][1] == L'-'))
            dwFiltNum++;

    if (dwFiltNum)
    {
        dwFiltNum = 0;

        for (int i = 0; i < argc; i++)
        {
            if ((argv[i][0] == L'/' || argv[i][0] == L'-') && (argv[i][1] == L'+' || argv[i][1] == L'-'))
            {
                Filter aFilter;
                if (FAILED(hr = GetFilterFromArg(pLog, argv[i], aFilter, aliasNames, columnNames)))
                {
                    log::Warning(pLog, hr, L"Failed to interpret parameter \"%s\": Ignored\r\n", argv[i]);
                }
                else
                {
                    filters.push_back(aFilter);
                    dwFiltNum++;
                }
            }
        }
    }
    return S_OK;
}

HRESULT FileInfoCommon::GetFilterFromArg(
    const logger& pLog,
    LPCWSTR szConstArg,
    Filter& filter,
    const ColumnNameDef aliasNames[],
    const ColumnNameDef columnNames[])
{
    HRESULT hr = E_FAIL;

    if (szConstArg[0] != L'/' && szConstArg[0] != L'-')
        return E_INVALIDARG;
    if (szConstArg[1] != L'-' && szConstArg[1] != L'+')
        return E_INVALIDARG;

    WCHAR* szArg = _wcsdup(szConstArg);
    if (szArg == nullptr)
        return E_OUTOFMEMORY;

    if (szArg[1] == L'+')
        filter.bInclude = true;
    else if (szArg[1] == L'-')
        filter.bInclude = false;
    else
    {
        free(szArg);
        return E_INVALIDARG;
    }

    WCHAR* pColon = wcschr(szArg, L':');
    if (!pColon)
    {
        free(szArg);
        return E_INVALIDARG;
    }

    *pColon = L'\0';
    filter.intent = FileInfo::GetIntentions(pLog, szArg + 2, aliasNames, columnNames);

    if (filter.intent == FILEINFO_NONE)
    {
        free(szArg);
        return E_INVALIDARG;
    }

    WCHAR* pFilter = pColon + 1;

    filter.type = FILEFILTER_NONE;

    if (!_wcsnicmp(pFilter, L"SizeLT=", wcslen(L"SizeLT=")))
    {
        filter.type = FILEFILTER_SIZELESS;
        if (FAILED(hr = GetFileSizeFromArg(pFilter + wcslen(L"SizeLT="), filter.filterdata.size)))
        {
            free(szArg);
            return hr;
        }
        filter.filterdata.size.LowPart++;
    }
    else if (!_wcsnicmp(pFilter, L"SizeGT=", wcslen(L"SizeGT=")))
    {
        filter.type = FILEFILTER_SIZEMORE;
        if (FAILED(hr = GetFileSizeFromArg(pFilter + wcslen(L"SizeGT="), filter.filterdata.size)))
        {
            free(szArg);
            return hr;
        }
        if (filter.filterdata.size.LowPart)
            filter.filterdata.size.LowPart--;
    }
    else if (!_wcsnicmp(pFilter, L"HasVersionInfo", wcslen(L"HasVersionInfo")))
        filter.type = FILEFILTER_VERSIONINFO;
    else if (!_wcsnicmp(pFilter, L"HasPE", wcslen(L"HasPE")))
        filter.type = FILEFILTER_PEHEADER;
    else if (!_wcsnicmp(pFilter, L"ExtScript", wcslen(L"ExtScript")))
        filter.type = FILEFILTER_EXTSCRIPT;
    else if (!_wcsnicmp(pFilter, L"ExtBinary", wcslen(L"ExtBinary")))
        filter.type = FILEFILTER_EXTBINARY;
    else if (!_wcsnicmp(pFilter, L"ExtArchive", wcslen(L"ExtArchive")))
        filter.type = FILEFILTER_EXTARCHIVE;
    else if (!_wcsnicmp(pFilter, L"Ext=", wcslen(L"Ext=")))
    {
        filter.type = FILEFILTER_EXTCUSTOM;
        filter.filterdata.extcustom = GetFilterExtCustomFromString(pFilter + wcslen(L"Ext="));
    }
    if (filter.type == FILEFILTER_NONE)
    {
        free(szArg);
        return E_INVALIDARG;
    }

    free(szArg);
    return S_OK;
}

void FileInfoCommon::FreeFilters(Filter* pFilters)
{
    Filter* pCurFilt = pFilters;
    if (pCurFilt)
        while (pCurFilt->type != FILEFILTER_NONE)
        {
            if (pCurFilt->type == FILEFILTER_EXTCUSTOM && pCurFilt->filterdata.extcustom)
            {
                WCHAR** pCurExt = pCurFilt->filterdata.extcustom;
                while (*pCurExt)
                {
                    free(*pCurExt);
                    pCurExt++;
                }
            }
            pCurFilt++;
        }
    delete[] pFilters;
}

HRESULT FileInfoCommon::ConfigItem_fileinfo_filter(ConfigItem& parent, LPWSTR szName, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(szName, dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"HasVersionInfo", HASVERSIONINFO, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"HasPE", HASPE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"SizeLT", SIZELT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"SizeGT", SIZEGT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"ExtBinary", EXTBINARY, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"ExtArchive", EXTARCHIVE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"Ext", EXT, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT FileInfoCommon::ConfigItem_fileinfo_add_filter(ConfigItem& parent, DWORD dwIndex)
{
    return ConfigItem_fileinfo_filter(parent, L"add", dwIndex);
}
HRESULT FileInfoCommon::ConfigItem_fileinfo_omit_filter(ConfigItem& parent, DWORD dwIndex)
{
    return ConfigItem_fileinfo_filter(parent, L"omit", dwIndex);
}

HRESULT FileInfoCommon::ConfigItem_fileinfo_column(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNode(L"columns", dwIndex, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChildNodeList(L"default", DEFAULT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(ConfigItem_fileinfo_add_filter, ADD)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(ConfigItem_fileinfo_omit_filter, OMIT)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"error", WRITERRORS, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}
