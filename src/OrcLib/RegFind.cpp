//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//
#include "stdafx.h"
#include "RegFind.h"
#include "FileFind.h"

#include "ConfigItem.h"
#include "ConfigFileReader.h"
#include "EmbeddedResource.h"

#include "MemoryStream.h"

#include "StructuredOutputWriter.h"
#include "TableOutputWriter.h"

#include "ParameterCheck.h"
#include "WideAnsi.h"

#include "ConfigFile_Common.h"

#include <sstream>
#include <iomanip>

using namespace std;
using namespace Orc;

RegFind::Match::KeyNameMatch::KeyNameMatch(const RegistryKey* const MatchingRegistryKey)
{
    if (MatchingRegistryKey != nullptr)
    {
        KeyName = MatchingRegistryKey->GetKeyName();
        ShortKeyName = MatchingRegistryKey->GetShortKeyName();
        ClassName = MatchingRegistryKey->GetKeyClassName();

        SubKeysCount = MatchingRegistryKey->GetSubKeysCount();
        ValuesCount = MatchingRegistryKey->GetValuesCount();

        LastModificationTime = MatchingRegistryKey->GetKeyLastModificationTime();
        Type = MatchingRegistryKey->GetKeyType();
    }
}

RegFind::Match::ValueNameMatch::ValueNameMatch(const RegistryValue* const MatchingRegsitryValue)
{

    if (MatchingRegsitryValue != nullptr)
    {
        const RegistryKey* const ParentKey = MatchingRegsitryValue->GetParentKey();
        if (ParentKey != nullptr)
        {
            KeyName = ParentKey->GetKeyName();
            ShortKeyName = ParentKey->GetShortKeyName();
            ClassName = ParentKey->GetKeyClassName();
            KeyType = ParentKey->GetKeyType();
            LastModificationTime = ParentKey->GetKeyLastModificationTime();
        }

        ValueName = MatchingRegsitryValue->GetValueName();
        ValueType = MatchingRegsitryValue->GetType();

        const BYTE* TmpDatas;
        DatasLength = MatchingRegsitryValue->GetDatas(&TmpDatas);

        if (TmpDatas != nullptr)
        {
            std::unique_ptr<BYTE> Tmp(new BYTE[DatasLength]);
            CopyMemory(Tmp.get(), TmpDatas, DatasLength);
            Datas.swap(Tmp);
        }
    }
}

HRESULT RegFind::Match::Write(const logger&, ITableOutput& output)
{
    DBG_UNREFERENCED_PARAMETER(output);

    for (const auto& aValueNameMatch : MatchingValues)
    {
        DBG_UNREFERENCED_PARAMETER(aValueNameMatch);
    }
    for (const auto& aKeyNameMatch : MatchingKeys)
    {
        DBG_UNREFERENCED_PARAMETER(aKeyNameMatch);
    }
    return S_OK;
}

HRESULT RegFind::Match::Write(
    const logger&,
    const std::shared_ptr<StructuredOutputWriter>& pWriter)
{
    string strMatchDescr = Term->GetDescription();

    pWriter->BeginElement(L"regfind_match");

    pWriter->WriteNameFormatedStringPair(L"description", L"%S", strMatchDescr.c_str());

    for (const auto& aValueNameMatch : MatchingValues)
    {
        pWriter->BeginElement(L"value");
        {
            pWriter->WriteNameFormatedStringPair(L"key", L"%S", aValueNameMatch.KeyName.c_str());
            pWriter->WriteNameFormatedStringPair(L"value", L"%S", aValueNameMatch.ValueName.c_str());

            for (UINT idx = 0; g_ValyeTypeDefinitions[idx].Type != RegMaxType; idx++)
            {
                if (aValueNameMatch.ValueType == g_ValyeTypeDefinitions[idx].Type)
                {
                    pWriter->WriteNameValuePair(L"type", g_ValyeTypeDefinitions[idx].szTypeName);
                    break;
                }
            }

            pWriter->WriteNameFileTimePair(L"lastmodified_key", aValueNameMatch.LastModificationTime);

            pWriter->WriteNameFormatedStringPair(L"data_size", L"%Iu", aValueNameMatch.DatasLength);
        }
        pWriter->EndElement(L"value");
    }
    for (const auto& aKeyNameMatch : MatchingKeys)
    {
        pWriter->BeginElement(L"key");
        {
            pWriter->WriteNameFormatedStringPair(L"key", L"%S", aKeyNameMatch.KeyName.c_str());
            pWriter->WriteNameValuePair(L"subkeys_count", aKeyNameMatch.SubKeysCount);
            pWriter->WriteNameValuePair(L"values_count", aKeyNameMatch.ValuesCount);
            pWriter->WriteNameFileTimePair(L"lastmodified_key", aKeyNameMatch.LastModificationTime);
        }
        pWriter->EndElement(L"key");
    }
    pWriter->EndElement(L"regfind_match");
    return S_OK;
}

ValueType RegFind::GetRegistryValueType(LPCWSTR szValueType)
{
    UINT i = 0;

    while (g_ValyeTypeDefinitions[i].Type != RegMaxType && i < _countof(g_ValyeTypeDefinitions))
    {
        if (!wcscmp(g_ValyeTypeDefinitions[i].szTypeName, szValueType))
            return g_ValyeTypeDefinitions[i].Type;
        i++;
    }
    return RegNone;
}

std::shared_ptr<RegFind::SearchTerm> RegFind::GetSearchTermFromConfig(const ConfigItem& item, logger pLog)
{
    HRESULT hr = E_FAIL;

    std::shared_ptr<RegFind::SearchTerm> retval = std::make_shared<RegFind::SearchTerm>();

    if (item[CONFIG_REGFIND_KEY])
    {
        WideToAnsi(pLog, (std::wstring_view) item[CONFIG_REGFIND_KEY], retval->m_strKeyName);
        retval->m_criteriaRequired =
            static_cast<RegFind::SearchTerm::Criteria>(retval->m_criteriaRequired | RegFind::SearchTerm::KEY_NAME);
    }
    if (item[CONFIG_REGFIND_KEY_REGEX])
    {
        WideToAnsi(pLog, (std::wstring_view) item[CONFIG_REGFIND_KEY_REGEX], retval->m_strKeyName);
        try
        {
            retval->m_regexKeyName.assign(retval->m_strKeyName, std::regex::ECMAScript | std::regex::icase);
        }
        catch (std::regex_error& e)
        {
            switch (e.code())
            {
                case std::regex_constants::error_collate:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error collate).\r\n",
                        item[CONFIG_REGFIND_KEY_REGEX].c_str(),
                        e.code());
                    break;
                case std::regex_constants::error_ctype:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error ctype).\r\n",
                        item[CONFIG_REGFIND_KEY_REGEX].c_str(),
                        e.code());
                    break;
                case std::regex_constants::error_escape:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error escape).\r\n",
                        item[CONFIG_REGFIND_KEY_REGEX].c_str(),
                        e.code());
                    break;
                case std::regex_constants::error_backref:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error backref).\r\n",
                        item[CONFIG_REGFIND_KEY_REGEX].c_str(),
                        e.code());
                    break;
                case std::regex_constants::error_brack:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error brack).\r\n",
                        item[CONFIG_REGFIND_KEY_REGEX].c_str(),
                        e.code());
                    break;
                case std::regex_constants::error_paren:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error paren).\r\n",
                        item[CONFIG_REGFIND_KEY_REGEX].c_str(),
                        e.code());
                    break;
                case std::regex_constants::error_brace:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error brace).\r\n",
                        item[CONFIG_REGFIND_KEY_REGEX].c_str(),
                        e.code());
                    break;
                case std::regex_constants::error_badbrace:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error badbrace).\r\n",
                        item[CONFIG_REGFIND_KEY_REGEX].c_str(),
                        e.code());
                    break;
                case std::regex_constants::error_range:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error range).\r\n",
                        item[CONFIG_REGFIND_KEY_REGEX].c_str(),
                        e.code());
                    break;
                case std::regex_constants::error_space:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error space).\r\n",
                        item[CONFIG_REGFIND_KEY_REGEX].c_str(),
                        e.code());
                    break;
                case std::regex_constants::error_badrepeat:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error badrepeat).\r\n",
                        item[CONFIG_REGFIND_KEY_REGEX].c_str(),
                        e.code());
                    break;
                case std::regex_constants::error_complexity:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error complexity).\r\n",
                        item[CONFIG_REGFIND_KEY_REGEX].c_str(),
                        e.code());
                    break;
                case std::regex_constants::error_stack:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error stack).\r\n",
                        item[CONFIG_REGFIND_KEY_REGEX].c_str(),
                        e.code());
                    break;
                default:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (unknown error 0x%x).\r\n",
                        item[CONFIG_REGFIND_KEY_REGEX].c_str(),
                        e.code());
                    break;
            }
            return nullptr;
        }
        retval->m_criteriaRequired = static_cast<RegFind::SearchTerm::Criteria>(
            retval->m_criteriaRequired | RegFind::SearchTerm::KEY_NAME_REGEX);
    }

    if (item[CONFIG_REGFIND_PATH])
    {
        WideToAnsi(pLog, (std::wstring_view) item[CONFIG_REGFIND_PATH], retval->m_strPathName);
        retval->m_criteriaRequired =
            static_cast<RegFind::SearchTerm::Criteria>(retval->m_criteriaRequired | RegFind::SearchTerm::KEY_PATH);
    }
    if (item[CONFIG_REGFIND_PATH_REGEX])
    {
        WideToAnsi(pLog, (std::wstring_view) item[CONFIG_REGFIND_PATH_REGEX], retval->m_strPathName);
        try
        {
            retval->m_regexPathName.assign(retval->m_strPathName, std::regex::ECMAScript | std::regex::icase);
        }
        catch (std::regex_error& e)
        {
            switch (e.code())
            {
                case std::regex_constants::error_collate:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error collate).\r\n",
                        item[CONFIG_REGFIND_PATH_REGEX].c_str());
                    break;
                case std::regex_constants::error_ctype:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error ctype).\r\n",
                        item[CONFIG_REGFIND_PATH_REGEX].c_str());
                    break;
                case std::regex_constants::error_escape:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error escape).\r\n",
                        item[CONFIG_REGFIND_PATH_REGEX].c_str());
                    break;
                case std::regex_constants::error_backref:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error backref).\r\n",
                        item[CONFIG_REGFIND_PATH_REGEX].c_str());
                    break;
                case std::regex_constants::error_brack:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error brack).\r\n",
                        item[CONFIG_REGFIND_PATH_REGEX].c_str());
                    break;
                case std::regex_constants::error_paren:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error paren).\r\n",
                        item[CONFIG_REGFIND_PATH_REGEX].c_str());
                    break;
                case std::regex_constants::error_brace:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error brace).\r\n",
                        item[CONFIG_REGFIND_PATH_REGEX].c_str());
                    break;
                case std::regex_constants::error_badbrace:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error badbrace).\r\n",
                        item[CONFIG_REGFIND_PATH_REGEX].c_str());
                    break;
                case std::regex_constants::error_range:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error range).\r\n",
                        item[CONFIG_REGFIND_PATH_REGEX].c_str());
                    break;
                case std::regex_constants::error_space:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error space).\r\n",
                        item[CONFIG_REGFIND_PATH_REGEX].c_str());
                    break;
                case std::regex_constants::error_badrepeat:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error badrepeat).\r\n",
                        item[CONFIG_REGFIND_PATH_REGEX].c_str());
                    break;
                case std::regex_constants::error_complexity:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error complexity).\r\n",
                        item[CONFIG_REGFIND_PATH_REGEX].c_str());
                    break;
                case std::regex_constants::error_stack:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error stack).\r\n",
                        item[CONFIG_REGFIND_PATH_REGEX].c_str());
                    break;
                default:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (unknown error 0x%x).\r\n",
                        item[CONFIG_REGFIND_PATH_REGEX].c_str(),
                        e.code());
                    break;
            }
            return nullptr;
        }

        retval->m_criteriaRequired = static_cast<RegFind::SearchTerm::Criteria>(
            retval->m_criteriaRequired | RegFind::SearchTerm::KEY_PATH_REGEX);
    }

    if (item[CONFIG_REGFIND_VALUE])
    {
        WideToAnsi(pLog, (std::wstring_view)item[CONFIG_REGFIND_VALUE], retval->m_strValueName);
        retval->m_criteriaRequired =
            static_cast<RegFind::SearchTerm::Criteria>(retval->m_criteriaRequired | RegFind::SearchTerm::VALUE_NAME);
    }

    if (item[CONFIG_REGFIND_VALUE_REGEX])
    {
        WideToAnsi(pLog, (std::wstring_view)item[CONFIG_REGFIND_VALUE_REGEX], retval->m_strValueName);
        try
        {
            retval->m_regexValueName.assign(retval->m_strValueName, std::regex::ECMAScript | std::regex::icase);
        }
        catch (std::regex_error& e)
        {
            switch (e.code())
            {
                case std::regex_constants::error_collate:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error collate).\r\n",
                        item[CONFIG_REGFIND_VALUE_REGEX].c_str());
                    break;
                case std::regex_constants::error_ctype:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error ctype).\r\n",
                        item[CONFIG_REGFIND_VALUE_REGEX].c_str());
                    break;
                case std::regex_constants::error_escape:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error escape).\r\n",
                        item[CONFIG_REGFIND_VALUE_REGEX].c_str());
                    break;
                case std::regex_constants::error_backref:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error backref).\r\n",
                        item[CONFIG_REGFIND_VALUE_REGEX].c_str());
                    break;
                case std::regex_constants::error_brack:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error brack).\r\n",
                        item[CONFIG_REGFIND_VALUE_REGEX].c_str());
                    break;
                case std::regex_constants::error_paren:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error paren).\r\n",
                        item[CONFIG_REGFIND_VALUE_REGEX].c_str());
                    break;
                case std::regex_constants::error_brace:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error brace).\r\n",
                        item[CONFIG_REGFIND_VALUE_REGEX].c_str());
                    break;
                case std::regex_constants::error_badbrace:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error badbrace).\r\n",
                        item[CONFIG_REGFIND_VALUE_REGEX].c_str());
                    break;
                case std::regex_constants::error_range:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error range).\r\n",
                        item[CONFIG_REGFIND_VALUE_REGEX].c_str());
                    break;
                case std::regex_constants::error_space:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error space).\r\n",
                        item[CONFIG_REGFIND_VALUE_REGEX].c_str());
                    break;
                case std::regex_constants::error_badrepeat:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error badrepeat).\r\n",
                        item[CONFIG_REGFIND_VALUE_REGEX].c_str());
                    break;
                case std::regex_constants::error_complexity:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error complexity).\r\n",
                        item[CONFIG_REGFIND_VALUE_REGEX].c_str());
                    break;
                case std::regex_constants::error_stack:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error stack).\r\n",
                        item[CONFIG_REGFIND_VALUE_REGEX].c_str());
                    break;
                default:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (unknown error 0x%x).\r\n",
                        item[CONFIG_REGFIND_VALUE_REGEX].c_str(),
                        e.code());
                    break;
            }
            return nullptr;
        }
        retval->m_criteriaRequired = static_cast<RegFind::SearchTerm::Criteria>(
            retval->m_criteriaRequired | RegFind::SearchTerm::VALUE_NAME_REGEX);
    }

    if (item[CONFIG_REGFIND_VALUE_TYPE])
    {
        retval->m_ValueType = RegFind::GetRegistryValueType(item[CONFIG_REGFIND_VALUE_TYPE].c_str());
        if (retval->m_ValueType == RegNone)
        {
            log::Error(
                pLog,
                E_INVALIDARG,
                L"Invalid registry value type %s\r\n",
                item[CONFIG_REGFIND_VALUE_TYPE].c_str());
            return nullptr;
        }
        retval->m_criteriaRequired =
            static_cast<RegFind::SearchTerm::Criteria>(retval->m_criteriaRequired | RegFind::SearchTerm::VALUE_TYPE);
    }

    if (item[CONFIG_REGFIND_DATA])
    {
        WideToAnsi(pLog, item[CONFIG_REGFIND_DATA].c_str(), retval->m_DataContent);
        retval->m_DataContent.SetCount(item[CONFIG_REGFIND_DATA].size());

        retval->m_WDataContent.SetData(
            (LPBYTE)item[CONFIG_REGFIND_DATA].c_str(),
            item[CONFIG_REGFIND_DATA].size() * sizeof(WCHAR));
        retval->m_WDataContent.SetCount(item[CONFIG_REGFIND_DATA].size() * sizeof(WCHAR));

        retval->m_criteriaRequired =
            static_cast<RegFind::SearchTerm::Criteria>(retval->m_criteriaRequired | RegFind::SearchTerm::DATA_CONTENT);
    }

    if (item[CONFIG_REGFIND_DATA_HEX])
    {
        std::wstring Data;
        size_t strSize;
        strSize = item[CONFIG_REGFIND_DATA_HEX].size();
        if (strSize == 0)
        {
            return nullptr;
        }

        // take care of cases where hex data length is not %2
        // add a "0" (eg. 0x123 or 1)
        if (strSize == 1)
        {
            Data = L"0"s + (const std::wstring&)item[CONFIG_REGFIND_DATA_HEX];
        }
        else if (strSize % 2)
        {
            if (wcsncmp(item[CONFIG_REGFIND_DATA_HEX].c_str(), L"0x", 2) == 0)
            {
                Data = L"0"s + ((const std::wstring&)item[CONFIG_REGFIND_DATA_HEX]).substr(2, std::string::npos);
            }
            else
                Data = L"0"s + (const std::wstring&)item[CONFIG_REGFIND_DATA_HEX];
        }
        else
        {
            Data = item[CONFIG_REGFIND_DATA_HEX];
        }

        if (FAILED(hr = GetBytesFromHexaString(Data.c_str(), static_cast<DWORD>(Data.size()), retval->m_DataContent)))
        {
            log::Error(
                pLog, E_INVALIDARG, L"Invalid bytes for content %s\r\n", item[CONFIG_REGFIND_DATA_HEX].c_str());
            return nullptr;
        }
        // Also initiate unicode version of pattern (in case pattern tested against SZ values)
        retval->m_WDataContent = retval->m_DataContent;

        retval->m_criteriaRequired =
            static_cast<RegFind::SearchTerm::Criteria>(retval->m_criteriaRequired | RegFind::SearchTerm::DATA_CONTENT);
    }

    if (item[CONFIG_REGFIND_DATA_SIZE])
    {
        LARGE_INTEGER li = {0};
        if (FAILED(hr = GetFileSizeFromArg(item[CONFIG_REGFIND_DATA_SIZE].c_str(), li)))
        {
            log::Error(
                pLog, hr, L"Invalid file size specification: %s\r\n", item[CONFIG_REGFIND_DATA_SIZE].c_str());
            return nullptr;
        }
        if (li.QuadPart < 0)
        {
            log::Error(
                pLog,
                hr,
                L"Invalid negative file size specification: %s\r\n",
                item[CONFIG_REGFIND_DATA_SIZE].c_str());
            return nullptr;
        }
        retval->m_ulDataSize = li.QuadPart;
        retval->m_criteriaRequired = static_cast<RegFind::SearchTerm::Criteria>(
            retval->m_criteriaRequired | RegFind::SearchTerm::DATA_SIZE_EQUAL);
    }

    if (item[CONFIG_REGFIND_DATA_SIZE_LT])
    {
        LARGE_INTEGER li = {0};
        if (FAILED(hr = GetFileSizeFromArg(item[CONFIG_REGFIND_DATA_SIZE_LT].c_str(), li)))
        {
            log::Error(
                pLog,
                hr,
                L"Invalid file size specification: %s\r\n",
                item[CONFIG_REGFIND_DATA_SIZE_LT].c_str());
            return nullptr;
        }
        if (li.QuadPart < 0)
        {
            log::Error(
                pLog,
                hr,
                L"Invalid negative file size specification: %s\r\n",
                item[CONFIG_REGFIND_DATA_SIZE].c_str());
            return nullptr;
        }
        if (li.QuadPart == 0)
        {
            log::Error(
                pLog,
                hr,
                L"Invalid zero file size specification: %s\r\n",
                item[CONFIG_REGFIND_DATA_SIZE].c_str());
            return nullptr;
        }
        retval->m_ulDataSizeHighLimit = li.QuadPart - 1;
        retval->m_criteriaRequired = static_cast<RegFind::SearchTerm::Criteria>(
            retval->m_criteriaRequired | RegFind::SearchTerm::DATA_SIZE_LESS);
    }

    if (item[CONFIG_REGFIND_DATA_SIZE_GT])
    {
        LARGE_INTEGER li = {0};
        if (FAILED(hr = GetFileSizeFromArg(item[CONFIG_REGFIND_DATA_SIZE_GT].c_str(), li)))
        {
            log::Error(
                pLog,
                hr,
                L"Invalid file size specification: %s\r\n",
                item[CONFIG_REGFIND_DATA_SIZE_GT].c_str());
            return nullptr;
        }
        if (li.QuadPart < 0)
        {
            log::Error(
                pLog,
                hr,
                L"Invalid negative file size specification: %s\r\n",
                item[CONFIG_REGFIND_DATA_SIZE].c_str());
            return nullptr;
        }

        retval->m_ulDataSizeLowLimit = li.QuadPart + 1;
        retval->m_criteriaRequired = static_cast<RegFind::SearchTerm::Criteria>(
            retval->m_criteriaRequired | RegFind::SearchTerm::DATA_SIZE_MORE);
    }

    if (item[CONFIG_REGFIND_DATA_SIZE_LE])
    {
        LARGE_INTEGER li = {0};
        if (FAILED(hr = GetFileSizeFromArg(item[CONFIG_REGFIND_DATA_SIZE_LE].c_str(), li)))
        {
            log::Error(
                pLog,
                hr,
                L"Invalid file size specification: %s\r\n",
                item[CONFIG_REGFIND_DATA_SIZE_LE].c_str());
            return nullptr;
        }
        if (li.QuadPart < 0)
        {
            log::Error(
                pLog,
                hr,
                L"Invalid negative file size specification: %s\r\n",
                item[CONFIG_REGFIND_DATA_SIZE].c_str());
            return nullptr;
        }

        retval->m_ulDataSizeHighLimit = li.QuadPart;
        retval->m_criteriaRequired = static_cast<RegFind::SearchTerm::Criteria>(
            retval->m_criteriaRequired | RegFind::SearchTerm::DATA_SIZE_LESS);
    }

    if (item[CONFIG_REGFIND_DATA_SIZE_GE])
    {
        LARGE_INTEGER li = {0};
        if (FAILED(hr = GetFileSizeFromArg(item[CONFIG_REGFIND_DATA_SIZE_GE].c_str(), li)))
        {
            log::Error(
                pLog,
                hr,
                L"Invalid file size specification: %s\r\n",
                item[CONFIG_REGFIND_DATA_SIZE_GE].c_str());
            return nullptr;
        }
        if (li.QuadPart < 0)
        {
            log::Error(
                pLog,
                hr,
                L"Invalid negative file size specification: %s\r\n",
                item[CONFIG_REGFIND_DATA_SIZE].c_str());
            return nullptr;
        }

        retval->m_ulDataSizeLowLimit = li.QuadPart;
        retval->m_criteriaRequired = static_cast<RegFind::SearchTerm::Criteria>(
            retval->m_criteriaRequired | RegFind::SearchTerm::DATA_SIZE_MORE);
    }

    if (item[CONFIG_REGFIND_DATA_REGEX])
    {

        WideToAnsi(pLog, item[CONFIG_REGFIND_DATA_REGEX].c_str(), retval->m_strRegexDataContentPattern);
        try
        {
            retval->m_regexDataContentPattern.assign(
                retval->m_strRegexDataContentPattern, std::regex::ECMAScript | std::regex::icase);
            retval->m_wregexDataContentPattern.assign(
                item[CONFIG_REGFIND_DATA_REGEX].c_str(), std::wregex::ECMAScript | std::wregex::icase);
        }
        catch (std::regex_error& e)
        {

            switch (e.code())
            {
                case std::regex_constants::error_collate:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error collate).\r\n",
                        item[CONFIG_REGFIND_DATA_REGEX].c_str());
                    break;
                case std::regex_constants::error_ctype:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error ctype).\r\n",
                        item[CONFIG_REGFIND_DATA_REGEX].c_str());
                    break;
                case std::regex_constants::error_escape:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error escape).\r\n",
                        item[CONFIG_REGFIND_DATA_REGEX].c_str());
                    break;
                case std::regex_constants::error_backref:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error backref).\r\n",
                        item[CONFIG_REGFIND_DATA_REGEX].c_str());
                    break;
                case std::regex_constants::error_brack:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error brack).\r\n",
                        item[CONFIG_REGFIND_DATA_REGEX].c_str());
                    break;
                case std::regex_constants::error_paren:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error paren).\r\n",
                        item[CONFIG_REGFIND_DATA_REGEX].c_str());
                    break;
                case std::regex_constants::error_brace:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error brace).\r\n",
                        item[CONFIG_REGFIND_DATA_REGEX].c_str());
                    break;
                case std::regex_constants::error_badbrace:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error badbrace).\r\n",
                        item[CONFIG_REGFIND_DATA_REGEX].c_str());
                    break;
                case std::regex_constants::error_range:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error range).\r\n",
                        item[CONFIG_REGFIND_DATA_REGEX].c_str());
                    break;
                case std::regex_constants::error_space:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error space).\r\n",
                        item[CONFIG_REGFIND_DATA_REGEX].c_str());
                    break;
                case std::regex_constants::error_badrepeat:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error badrepeat).\r\n",
                        item[CONFIG_REGFIND_DATA_REGEX].c_str());
                    break;
                case std::regex_constants::error_complexity:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error complexity).\r\n",
                        item[CONFIG_REGFIND_DATA_REGEX].c_str());
                    break;
                case std::regex_constants::error_stack:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (error stack).\r\n",
                        item[CONFIG_REGFIND_DATA_REGEX].c_str());
                    break;
                default:
                    log::Error(
                        pLog,
                        E_INVALIDARG,
                        L"Error with regex \"%S\" (unknown error 0x%x).\r\n",
                        item[CONFIG_REGFIND_DATA_REGEX].c_str(),
                        e.code());
                    break;
            }
            return nullptr;
        }

        retval->m_criteriaRequired = static_cast<RegFind::SearchTerm::Criteria>(
            retval->m_criteriaRequired | RegFind::SearchTerm::DATA_CONTENT_REGEX);
    }

    if (item[CONFIG_REGFIND_DATA_CONTAINS])
    {
        WideToAnsi(pLog, item[CONFIG_REGFIND_DATA_CONTAINS].c_str(), retval->m_DataContentContains);
        retval->m_DataContentContains.SetCount(item[CONFIG_REGFIND_DATA_CONTAINS].size());

        retval->m_WDataContentContains.SetCount(item[CONFIG_REGFIND_DATA_CONTAINS].size() * sizeof(WCHAR));
        retval->m_WDataContentContains.SetData(
            (const LPBYTE)(item[CONFIG_REGFIND_DATA_CONTAINS].c_str()),
            item[CONFIG_REGFIND_DATA_CONTAINS].size() * sizeof(WCHAR));

        retval->m_criteriaRequired =
            static_cast<RegFind::SearchTerm::Criteria>(retval->m_criteriaRequired | RegFind::SearchTerm::DATA_CONTAINS);
    }

    if (item[CONFIG_REGFIND_DATA_CONTAINS_HEX])
    {
        std::wstring Data;
        size_t strSize;
        strSize = item[CONFIG_REGFIND_DATA_CONTAINS_HEX].size();

        if (strSize == 0)
        {
            return nullptr;
        }
        // take care of cases where hex data length is not %2
        // add a "0" (eg. 0x123 or 1)
        if (strSize == 1)
        {
            Data = L"0"s + item[CONFIG_REGFIND_DATA_CONTAINS_HEX].c_str();
        }
        else if (strSize % 2)
        {
            if (wcsncmp(item[CONFIG_REGFIND_DATA_CONTAINS_HEX].c_str(), L"0x", 2) == 0)
            {
                Data = L"0"s + ((const std::wstring&) item[CONFIG_REGFIND_DATA_CONTAINS_HEX]).substr(2, std::string::npos);
            }
            else
                Data = L"0"s + (const std::wstring&)item[CONFIG_REGFIND_DATA_CONTAINS_HEX];
        }
        else
        {
            Data = item[CONFIG_REGFIND_DATA_CONTAINS_HEX];
        }
        if (FAILED(
                hr = GetBytesFromHexaString(
                    Data.c_str(), static_cast<DWORD>(Data.size()), retval->m_DataContentContains)))
        {
            log::Error(
                pLog,
                E_INVALIDARG,
                L"Invalid bytes for content %s\r\n",
                item[CONFIG_REGFIND_DATA_CONTAINS_HEX].c_str());
            return nullptr;
        }

        // also initiate unicode version of pattern (in case pattern is tested against SZ values)
        retval->m_WDataContentContains = retval->m_DataContentContains;

        retval->m_criteriaRequired =
            static_cast<RegFind::SearchTerm::Criteria>(retval->m_criteriaRequired | RegFind::SearchTerm::DATA_CONTAINS);
    }

    return retval;
}

HRESULT RegFind::AddRegFindFromConfig(const std::vector<ConfigItem>& items)
{
    for (const auto& item : items)
    {
        auto term = GetSearchTermFromConfig(item, _L_);

        if (term != nullptr)
        {
            HRESULT hr = E_FAIL;
            if (FAILED(hr = AddSearchTerm(term)))
            {
                log::Error(_L_, hr, L"Failed to add registry search term\r\n");
            }
        }
    }
    return S_OK;
}

HRESULT RegFind::AddRegFindFromTemplate(const std::vector<ConfigItem>& items)
{

    HRESULT hr = E_FAIL;

    for (const auto& item : items)
    {
        if (!item[CONFIG_TEMPLATE_NAME])
        {
            log::Error(_L_, hr = E_INVALIDARG, L"Missing mandatory template name.\r\n");
            return hr;
        }

        if (!item[CONFIG_TEMPLATE_LOCATION])
        {
            log::Error(_L_, hr = E_INVALIDARG, L"Missing mandatory template name.\r\n");
            return hr;
        }

        ConfigFileReader TemplateConfig(_L_, false);
        ConfigItem NewItem;
        Orc::Config::Common::regfind_template(NewItem);

        if (EmbeddedResource::IsResourceBased(item[CONFIG_TEMPLATE_LOCATION].c_str()))
        {

            CBinaryBuffer buffer;

            if (SUCCEEDED(
                    hr =
                        EmbeddedResource::ExtractToBuffer(_L_, item[CONFIG_TEMPLATE_LOCATION].c_str(), buffer)))
            {
                // Config file is used, let's read it
                auto memstream = std::make_shared<MemoryStream>(_L_);

                if (FAILED(hr = memstream->OpenForReadOnly(buffer.GetData(), buffer.GetCount())))
                {
                    log::Error(_L_, hr, L"Failed to create stream for memory buffer\r\n");
                    return hr;
                }

                if (FAILED(hr = TemplateConfig.ReadConfig(memstream, NewItem)))
                {
                    log::Error(
                        _L_,
                        hr,
                        L"Failed to read config resource %s (0x%lx)\r\n",
                        item[CONFIG_TEMPLATE_LOCATION].c_str());
                    return hr;
                }
            }
            else
            {
                log::Error(
                    _L_,
                    hr,
                    L"Failed to extract config from resource %s (0x%lx)\r\n",
                    item[CONFIG_TEMPLATE_LOCATION].c_str());
                return hr;
            }
        }
        else
        {
            if (FAILED(hr = TemplateConfig.ReadConfig(item[CONFIG_TEMPLATE_LOCATION].c_str(), NewItem)))
            {
                log::Error(
                    _L_,
                    hr,
                    L"Failed to open template file \"%s\"\r\n",
                    item[CONFIG_TEMPLATE_LOCATION].c_str());
                return hr;
            }
        }

        for (const auto& TemplateItem : NewItem[CONFIG_HIVE_REGFIND_TEMPLATE].NodeList)
        {
            auto term = GetSearchTermFromConfig(TemplateItem, _L_);

            if (term != nullptr)
            {
                term->SetTermName(item[CONFIG_TEMPLATE_NAME]);
                if (FAILED(hr = AddSearchTerm(term)))
                {
                    log::Error(_L_, hr, L"Failed to add registry search term\r\n");
                }
            }
        }
    }

    return S_OK;
}

std::string RegFind::SearchTerm::GetCriteriaDescription() const
{
    std::stringstream stream;

    bool bFirst = true;

    if (m_criteriaRequired & SearchTerm::Criteria::KEY_NAME)
    {
        stream << "KEY_NAME";
        bFirst = false;
    }
    if (m_criteriaRequired & SearchTerm::Criteria::KEY_NAME_REGEX)
    {
        if (!bFirst)
            stream << " | ";
        stream << "KEY_NAME_REGEX";
        bFirst = false;
    }
    if (m_criteriaRequired & SearchTerm::Criteria::KEY_PATH)
    {
        if (!bFirst)
            stream << "|";
        stream << "KEY_PATH";
        bFirst = false;
    }
    if (m_criteriaRequired & SearchTerm::Criteria::KEY_PATH_REGEX)
    {
        if (!bFirst)
            stream << "|";
        stream << "KEY_PATH_REGEX";
        bFirst = false;
    }
    if (m_criteriaRequired & SearchTerm::Criteria::VALUE_NAME)
    {
        if (!bFirst)
            stream << " | ";
        stream << "VALUE_NAME";
        bFirst = false;
    }
    if (m_criteriaRequired & SearchTerm::Criteria::VALUE_NAME_REGEX)
    {
        if (!bFirst)
            stream << " | ";
        stream << "VALUE_NAME_REGEX";
        bFirst = false;
    }
    if (m_criteriaRequired & SearchTerm::Criteria::DATA_CONTENT)
    {
        if (!bFirst)
            stream << " | ";
        stream << "DATA_CONTENT";
        bFirst = false;
    }
    if (m_criteriaRequired & SearchTerm::Criteria::DATA_CONTENT_REGEX)
    {
        if (!bFirst)
            stream << " | ";
        stream << "DATA_CONTENT_REGEX ";
        bFirst = false;
    }

    if (m_criteriaRequired & SearchTerm::Criteria::DATA_SIZE_EQUAL)
    {
        if (!bFirst)
            stream << " | ";
        stream << "DATA_SIZE_EQUAL";
        bFirst = false;
    }
    if (m_criteriaRequired & SearchTerm::Criteria::DATA_SIZE_LESS)
    {
        if (!bFirst)
            stream << " | ";
        stream << "DATA_SIZE_LESS";
        bFirst = false;
    }
    if (m_criteriaRequired & SearchTerm::Criteria::DATA_SIZE_MORE)
    {
        if (!bFirst)
            stream << " | ";
        stream << "DATA_SIZE_MORE";
        bFirst = false;
    }
    if (m_criteriaRequired & SearchTerm::Criteria::VALUE_TYPE)
    {
        if (!bFirst)
            stream << " | ";
        stream << "VALUE_TYPE";
        bFirst = false;
    }

    // Support of return value optimization by compiler
    return stream.str();
}

const std::wstring& RegFind::SearchTerm::GetTermName() const
{
    return m_TermClassName;
}

void RegFind::SearchTerm::SetTermName(const std::wstring& wstrName)
{
    m_TermClassName = wstrName;
}

void RegFind::PrintSpecs() const
{
    log::Info(_L_, L"\r\nRegistry search details:\r\n");

    for (const auto& e : m_ExactKeyNameSpecs)
    {
        log::Info(_L_, L"%S", e.second->GetDescription().c_str());
    }

    for (const auto& e : m_ExactKeyPathSpecs)
    {
        log::Info(_L_, L"%S", e.second->GetDescription().c_str());
    }

    for (const auto& e : m_ExactValueNameSpecs)
    {
        log::Info(_L_, L"%S", e.second->GetDescription().c_str());
    }

    for (const auto& e : m_Specs)
    {
        log::Info(_L_, L"%S", e->GetDescription().c_str());
    }
}

std::string RegFind::SearchTerm::GetDescription() const
{
    std::stringstream stream;
    std::string retval;

    bool bFirst = true;

    if (m_criteriaRequired & SearchTerm::Criteria::KEY_NAME)
    {
        stream << "KeyName is " << m_strKeyName;
        bFirst = false;
    }

    if (m_criteriaRequired & SearchTerm::Criteria::KEY_NAME_REGEX)
    {
        if (!bFirst)
            stream << ", ";
        stream << "KeyName matches regex " << m_strKeyName;
        bFirst = false;
    }

    if (m_criteriaRequired & SearchTerm::Criteria::KEY_PATH)
    {
        if (!bFirst)
            stream << ", ";
        stream << "KeyPath is " << m_strPathName;
        bFirst = false;
    }

    if (m_criteriaRequired & SearchTerm::Criteria::KEY_PATH_REGEX)
    {
        if (!bFirst)
            stream << ", ";
        stream << "KeyPath matches regex " << m_strPathName;
        bFirst = false;
    }

    if (m_criteriaRequired & SearchTerm::Criteria::VALUE_NAME)
    {
        if (!bFirst)
            stream << ", ";
        stream << "Name is " << m_strValueName;
        bFirst = false;
    }

    if (m_criteriaRequired & SearchTerm::Criteria::VALUE_NAME_REGEX)
    {
        if (!bFirst)
            stream << ", ";
        stream << "Name matches regex " << m_strValueName;
        bFirst = false;
    }

    if (m_criteriaRequired & SearchTerm::Criteria::VALUE_TYPE)
    {
        if (!bFirst)
            stream << ", ";
        stream << "Value type is " << m_ValueType;
        bFirst = false;
    }

    if (m_criteriaRequired & SearchTerm::Criteria::DATA_SIZE_EQUAL)
    {
        if (!bFirst)
            stream << ", ";
        stream << "Data size is " << m_ulDataSize;
        bFirst = false;
    }

    if (m_criteriaRequired & SearchTerm::Criteria::DATA_SIZE_LESS)
    {
        if (!bFirst)
            stream << ", ";
        stream << "Data size is less than " << m_ulDataSizeHighLimit;
        bFirst = false;
    }

    if (m_criteriaRequired & SearchTerm::Criteria::DATA_SIZE_MORE)
    {
        if (!bFirst)
            stream << ", ";
        stream << "Data size is more than " << m_ulDataSizeLowLimit;
        bFirst = false;
    }

    if (m_criteriaRequired & SearchTerm::Criteria::DATA_CONTENT)
    {
        if (!bFirst)
            stream << ", ";
        stream << "Data is 0x" << std::hex << std::setfill('0') << std::setw(m_DataContent.GetCount() * 2)
               << _byteswap_ulong(*((DWORD*)m_DataContent.GetData())) << std::dec;
        bFirst = false;
    }

    if (m_criteriaRequired & SearchTerm::Criteria::DATA_CONTENT_REGEX)
    {
        if (!bFirst)
            stream << ", ";
        stream << "Data matches regex " << m_strRegexDataContentPattern;
        bFirst = false;
    }

    if (m_criteriaRequired & SearchTerm::Criteria::DATA_CONTAINS)
    {
        if (!bFirst)
            stream << ", ";
        stream << "Data contains " << std::hex;
        bFirst = false;
        size_t i;
        for (i = 0; i < m_DataContentContains.GetCount(); i++)
        {
            stream << m_DataContentContains[i];
        }
        stream << std::dec;
    }
    // Support of return value optimization by compiler
    return stream.str();
}

HRESULT RegFind::AddSearchTerm(const std::shared_ptr<RegFind::SearchTerm>& pMatch)
{
    bool bExact = false;
    if ((pMatch->m_criteriaRequired & SearchTerm::Criteria::KEY_NAME)
        && !((pMatch->m_criteriaRequired & SearchTerm::Criteria::VALUE_NAME)))
    {
        m_ExactKeyNameSpecs.insert(TermMap::value_type(pMatch->m_strKeyName, pMatch));
        bExact = true;
    }
    if ((pMatch->m_criteriaRequired & SearchTerm::Criteria::KEY_PATH)
        && !((pMatch->m_criteriaRequired & SearchTerm::Criteria::VALUE_NAME)))
    {
        m_ExactKeyPathSpecs.insert(TermMap::value_type(pMatch->m_strPathName, pMatch));
        bExact = true;
    }
    if (pMatch->m_criteriaRequired & SearchTerm::Criteria::VALUE_NAME)
    {
        m_ExactValueNameSpecs.insert(TermMap::value_type(pMatch->m_strValueName, pMatch));
        bExact = true;
    }

    if (!bExact)
    {
        m_Specs.push_back(pMatch);
    }

    return S_OK;
}

// Name specs: Only depend on KeyName
RegFind::SearchTerm::Criteria
RegFind::ExactKeyName(const std::shared_ptr<SearchTerm>& aTerm, const RegistryKey* const Regkey) const
{
    if (Regkey == nullptr)
        return RegFind::SearchTerm::Criteria::NONE;

    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::KEY_NAME)
    {

        if (aTerm->m_strKeyName.empty() || Regkey->GetShortKeyName().empty())
            return RegFind::SearchTerm::Criteria::NONE;

        if (_strnicmp(aTerm->m_strKeyName.c_str(), Regkey->GetShortKeyName().c_str(), aTerm->m_strKeyName.length())
            == 0)
            return SearchTerm::Criteria::KEY_NAME;
    }
    return RegFind::SearchTerm::Criteria::NONE;
}

RegFind::SearchTerm::Criteria
RegFind::RegexKeyName(const std::shared_ptr<SearchTerm>& aTerm, const RegistryKey* const Regkey) const
{
    if (Regkey == nullptr)
        return RegFind::SearchTerm::Criteria::NONE;

    if (Regkey->GetShortKeyName().empty() || aTerm->m_regexKeyName._Empty())
    {
        return SearchTerm::Criteria::NONE;
    }

    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::KEY_NAME_REGEX)
    {
        if (std::regex_match(Regkey->GetShortKeyName().c_str(), aTerm->m_regexKeyName))
            return SearchTerm::Criteria::KEY_NAME_REGEX;
    }

    return SearchTerm::Criteria::NONE;
}

RegFind::SearchTerm::Criteria RegFind::MatchKeyName(
    const std::shared_ptr<SearchTerm>& aTerm,
    SearchTerm::Criteria required,
    const RegistryKey* const Regkey) const
{
    DBG_UNREFERENCED_PARAMETER(required);
    SearchTerm::Criteria matchedCriteria = SearchTerm::Criteria::NONE;
    if (Regkey == nullptr)
        return RegFind::SearchTerm::Criteria::NONE;

    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::KEY_NAME)
    {
        matchedCriteria = static_cast<RegFind::SearchTerm::Criteria>(ExactKeyName(aTerm, Regkey) | matchedCriteria);
    }

    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::KEY_NAME_REGEX)
    {
        matchedCriteria = static_cast<RegFind::SearchTerm::Criteria>(RegexKeyName(aTerm, Regkey) | matchedCriteria);
    }

    return matchedCriteria;
}

// Name specs: Only depend on KeyPath
RegFind::SearchTerm::Criteria
RegFind::ExactKeyPath(const std::shared_ptr<SearchTerm>& aTerm, const RegistryKey* const Regkey) const
{
    if (Regkey == nullptr)
        return RegFind::SearchTerm::Criteria::NONE;

    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::KEY_PATH)
    {

        if (aTerm->m_strPathName.empty() || Regkey->GetKeyName().empty())
            return RegFind::SearchTerm::Criteria::NONE;

        if (_strnicmp(aTerm->m_strPathName.c_str(), Regkey->GetKeyName().c_str(), Regkey->GetKeyName().length()) == 0)
            return SearchTerm::Criteria::KEY_PATH;
    }
    return RegFind::SearchTerm::Criteria::NONE;
}

RegFind::SearchTerm::Criteria
RegFind::RegexKeyPath(const std::shared_ptr<SearchTerm>& aTerm, const RegistryKey* const Regkey) const
{
    if (Regkey == nullptr)
        return RegFind::SearchTerm::Criteria::NONE;

    if (Regkey->GetKeyName().empty() || aTerm->m_regexPathName._Empty())
    {
        return SearchTerm::Criteria::NONE;
    }

    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::KEY_PATH_REGEX)
    {
        if (std::regex_match(Regkey->GetKeyName().c_str(), aTerm->m_regexPathName))
            return SearchTerm::Criteria::KEY_PATH_REGEX;
    }

    return SearchTerm::Criteria::NONE;
}

RegFind::SearchTerm::Criteria RegFind::MatchKeyPath(
    const std::shared_ptr<SearchTerm>& aTerm,
    SearchTerm::Criteria required,
    const RegistryKey* const Regkey) const
{
    DBG_UNREFERENCED_PARAMETER(required);

    SearchTerm::Criteria matchedCriteria = SearchTerm::Criteria::NONE;
    if (Regkey == nullptr)
        return RegFind::SearchTerm::Criteria::NONE;

    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::KEY_PATH)
    {
        matchedCriteria = static_cast<RegFind::SearchTerm::Criteria>(ExactKeyPath(aTerm, Regkey) | matchedCriteria);
    }

    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::KEY_PATH_REGEX)
    {
        matchedCriteria = static_cast<RegFind::SearchTerm::Criteria>(RegexKeyPath(aTerm, Regkey) | matchedCriteria);
    }

    return matchedCriteria;
}

// Full path specs: Only depend on ValueName
RegFind::SearchTerm::Criteria
RegFind::ExactValueName(const std::shared_ptr<SearchTerm>& aTerm, const RegistryValue* const RegValue) const
{
    if (RegValue == nullptr)
        return RegFind::SearchTerm::Criteria::NONE;
    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::VALUE_NAME)
    {

        if (_strnicmp(aTerm->m_strValueName.c_str(), RegValue->GetValueName().c_str(), aTerm->m_strValueName.length())
            == 0)
            return SearchTerm::Criteria::VALUE_NAME;
    }
    return RegFind::SearchTerm::Criteria::NONE;
}

RegFind::SearchTerm::Criteria
RegFind::RegexValueName(const std::shared_ptr<SearchTerm>& aTerm, const RegistryValue* const RegValue) const
{
    if (RegValue == nullptr)
        return RegFind::SearchTerm::Criteria::NONE;

    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::VALUE_NAME_REGEX)
    {
        // check regex for emptiness but do not check value name: default value has an empty name!
        if (aTerm->m_regexValueName._Empty())
            return SearchTerm::Criteria::NONE;

        if (std::regex_match(RegValue->GetValueName().c_str(), aTerm->m_regexValueName))
            return SearchTerm::Criteria::VALUE_NAME_REGEX;
    }
    return SearchTerm::Criteria::NONE;
}

RegFind::SearchTerm::Criteria RegFind::MatchValueName(
    const std::shared_ptr<RegFind::SearchTerm>& aTerm,
    RegFind::SearchTerm::Criteria required,
    const RegistryValue* const RegValue) const
{
    DBG_UNREFERENCED_PARAMETER(required);
    SearchTerm::Criteria matchedCriteria = SearchTerm::Criteria::NONE;
    if (RegValue == nullptr)
        return RegFind::SearchTerm::Criteria::NONE;

    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::VALUE_NAME)
    {
        matchedCriteria = static_cast<RegFind::SearchTerm::Criteria>(ExactValueName(aTerm, RegValue) | matchedCriteria);
    }
    else if (aTerm->m_criteriaRequired & SearchTerm::Criteria::VALUE_NAME_REGEX)
    {
        matchedCriteria = static_cast<RegFind::SearchTerm::Criteria>(RegexValueName(aTerm, RegValue) | matchedCriteria);
    }

    return matchedCriteria;
}

// Data specs: only depend on Datas
RegFind::SearchTerm::Criteria
RegFind::ExactDatas(const std::shared_ptr<SearchTerm>& aTerm, const RegistryValue* const RegValue) const
{
    if (RegValue == nullptr)
        return RegFind::SearchTerm::Criteria::NONE;

    const BYTE* pDatas;
    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::DATA_CONTENT)
    {
        size_t ulSize;
        if ((aTerm->m_DataContent.GetData() == nullptr) || ((ulSize = RegValue->GetDatas(&pDatas)) == 0))
            return SearchTerm::Criteria::NONE;
        if (ulSize < aTerm->m_DataContent.GetCount())
        {
            return SearchTerm::Criteria::NONE;
        }

        size_t i;
        size_t dwCurrentStrSize;

        switch (RegValue->GetType())
        {
            case ValueType::RegMultiSZ:
                i = 0;

                while (i < ulSize)
                {

                    dwCurrentStrSize = wcslen((WCHAR*)(pDatas + i)) * sizeof(WCHAR);
                    if (aTerm->m_WDataContent.GetCount() != dwCurrentStrSize)
                    {
                        i = i + 2 + dwCurrentStrSize;
                        continue;
                    }

                    if (_wcsnicmp(
                            (WCHAR*)(pDatas + i),
                            (WCHAR*)aTerm->m_WDataContent.GetData(),
                            dwCurrentStrSize / sizeof(WCHAR))
                        == 0)
                    {
                        return SearchTerm::Criteria::DATA_CONTENT;
                        break;
                    }
                    i = i + 2 + dwCurrentStrSize;
                }
                break;
            case ValueType::RegSZ:
            case ValueType::ExpandSZ:

                if (_wcsnicmp(
                        (WCHAR*)pDatas,
                        (WCHAR*)aTerm->m_WDataContent.GetData(),
                        static_cast<size_t>(aTerm->m_WDataContent.GetCount() / sizeof(WCHAR)))
                    == 0)
                {
                    return SearchTerm::Criteria::DATA_CONTENT;
                }
                break;
            case ValueType::RegDWORD:
                // take care of endianness
                if (aTerm->m_DataContent.GetCount() > 4)
                {
                    break;
                }

                if (ulSize == 4)
                {
                    DWORD dwSearchData = _byteswap_ulong(*((DWORD*)aTerm->m_DataContent.GetData()));
                    DWORD dwData = *((DWORD*)pDatas);

                    if (dwSearchData == dwData)
                    {
                        return SearchTerm::Criteria::DATA_CONTENT;
                    }
                }
                break;

            case ValueType::RegQWORD:
                // take care of endianness
                if (aTerm->m_DataContent.GetCount() > 8)
                {
                    break;
                }

                if (ulSize == 8)
                {
                    DWORDLONG dwlSearchData = _byteswap_uint64(*((DWORDLONG*)aTerm->m_DataContent.GetData()));
                    DWORDLONG dwlData = *((DWORDLONG*)pDatas);

                    if (dwlSearchData == dwlData)
                    {
                        return SearchTerm::Criteria::DATA_CONTENT;
                    }
                }
                break;

            default:
                if (memcmp(pDatas, aTerm->m_DataContent.GetData(), static_cast<size_t>(aTerm->m_DataContent.GetCount()))
                    == 0)
                    return SearchTerm::Criteria::DATA_CONTENT;
                break;
        }
    }
    return RegFind::SearchTerm::Criteria::NONE;
}

RegFind::SearchTerm::Criteria
RegFind::RegexDatas(const std::shared_ptr<SearchTerm>& aTerm, const RegistryValue* const RegValue) const
{
    SearchTerm::Criteria matchedCriteria = SearchTerm::Criteria::NONE;

    if (RegValue == nullptr)
        return RegFind::SearchTerm::Criteria::NONE;

    const BYTE* pDatas;
    size_t DatasSize;
    if ((DatasSize = RegValue->GetDatas(&pDatas)) == 0)
        return SearchTerm::Criteria::NONE;
    if (pDatas == nullptr)
        return SearchTerm::Criteria::NONE;

    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::DATA_CONTENT_REGEX)
    {
        // Choose the regex depending on value type

        size_t i, CurrentStrSize;
        switch (RegValue->GetType())
        {
            case ValueType::RegMultiSZ:
                i = 0;

                while (i < DatasSize)
                {
                    CurrentStrSize = wcslen((WCHAR*)(pDatas + i));

                    if (std::regex_match(
                            (WCHAR*)(pDatas + i),
                            (WCHAR*)(pDatas + i + CurrentStrSize * sizeof(WCHAR)),
                            aTerm->m_wregexDataContentPattern))
                    {
                        matchedCriteria = static_cast<SearchTerm::Criteria>(
                            SearchTerm::Criteria::DATA_CONTENT_REGEX | matchedCriteria);
                        break;
                    }
                    i = i + 2 + CurrentStrSize * sizeof(WCHAR);
                }
                break;
            case ValueType::RegSZ:
            case ValueType::ExpandSZ:

                if (std::regex_match(
                        (WCHAR*)pDatas, ((WCHAR*)pDatas) + wcslen((WCHAR*)pDatas), aTerm->m_wregexDataContentPattern))
                    matchedCriteria =
                        static_cast<SearchTerm::Criteria>(SearchTerm::Criteria::DATA_CONTENT_REGEX | matchedCriteria);

                break;
            case ValueType::RegBin:

                if (std::regex_match(pDatas, pDatas + DatasSize, aTerm->m_regexDataContentPattern))
                    matchedCriteria =
                        static_cast<SearchTerm::Criteria>(SearchTerm::Criteria::DATA_CONTENT_REGEX | matchedCriteria);

                // also check with unicode pattern
                if (std::regex_match((WCHAR*)pDatas, (WCHAR*)(pDatas + DatasSize), aTerm->m_wregexDataContentPattern))
                    matchedCriteria =
                        static_cast<SearchTerm::Criteria>(SearchTerm::Criteria::DATA_CONTENT_REGEX | matchedCriteria);
                break;
            case ValueType::RegDWORD:
            case ValueType::RegDWORDBE:
            case ValueType::RegQWORD:
            default:
                break;
        }
    }

    return matchedCriteria;
}

RegFind::SearchTerm::Criteria
RegFind::DatasContains(const std::shared_ptr<SearchTerm>& aTerm, const RegistryValue* const RegValue) const
{
    RegFind::SearchTerm::Criteria matchedSpec = RegFind::SearchTerm::Criteria::NONE;

    if (RegValue == nullptr)
        return RegFind::SearchTerm::Criteria::NONE;

    CBinaryBuffer Pattern;
    // Use unicode version of datas for SZ data types
    switch (RegValue->GetType())
    {
        case ValueType::RegDWORD:
        case ValueType::RegDWORDBE:
        case ValueType::RegQWORD:
            return RegFind::SearchTerm::Criteria::NONE;
        case ValueType::RegSZ:
        case ValueType::ExpandSZ:
        case ValueType::RegMultiSZ:
            Pattern.SetData(aTerm->m_WDataContentContains.GetData(), aTerm->m_WDataContentContains.GetCount());
            break;
        default:
            Pattern.SetData(aTerm->m_DataContentContains.GetData(), aTerm->m_DataContentContains.GetCount());
            break;
    }

    boost::algorithm::boyer_moore<BYTE*> boyermoore(begin(Pattern), end(Pattern));
    CBinaryBuffer Buffer;
    const BYTE* Datas;
    size_t DatasSize;

    DatasSize = RegValue->GetDatas(&Datas);
    Buffer.SetData((LPBYTE)Datas, DatasSize);
    Buffer.SetCount(DatasSize);

    auto it = boyermoore(begin(Buffer), end(Buffer));

    auto nothing_found = std::make_pair(end(Buffer), end(Buffer));

    if (it != nothing_found)
    {
        matchedSpec = static_cast<RegFind::SearchTerm::Criteria>(
            matchedSpec | (RegFind::SearchTerm::Criteria::DATA_CONTAINS & aTerm->m_criteriaRequired));
        return matchedSpec;
    }

    return matchedSpec;
}

RegFind::SearchTerm::Criteria
RegFind::DatasSizeMatch(const std::shared_ptr<SearchTerm>& aTerm, const RegistryValue* const RegValue) const
{
    SearchTerm::Criteria matchedCriteria = SearchTerm::Criteria::NONE;

    if (RegValue == nullptr)
        return RegFind::SearchTerm::Criteria::NONE;

    const BYTE* pDatas;
    size_t ulSize;
    if (((ulSize = RegValue->GetDatas(&pDatas)) == 0) || (pDatas == nullptr))
        return RegFind::SearchTerm::Criteria::NONE;

    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::DATA_SIZE_EQUAL)
    {
        if (aTerm->m_ulDataSize == ulSize)
            matchedCriteria =
                static_cast<SearchTerm::Criteria>(SearchTerm::Criteria::DATA_SIZE_EQUAL | matchedCriteria);
    }
    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::DATA_SIZE_LESS)
    {
        if (aTerm->m_ulDataSizeHighLimit >= ulSize)
            matchedCriteria = static_cast<SearchTerm::Criteria>(SearchTerm::Criteria::DATA_SIZE_LESS | matchedCriteria);
    }
    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::DATA_SIZE_MORE)
    {
        if (aTerm->m_ulDataSizeLowLimit <= ulSize)
            matchedCriteria = static_cast<SearchTerm::Criteria>(SearchTerm::Criteria::DATA_SIZE_MORE | matchedCriteria);
    }
    return matchedCriteria;
}

RegFind::SearchTerm::Criteria RegFind::MatchDataAndSize(
    const std::shared_ptr<SearchTerm>& aTerm,
    SearchTerm::Criteria required,
    const RegistryValue* const RegValue) const
{
    DBG_UNREFERENCED_PARAMETER(required);

    if (RegValue == nullptr)
        return RegFind::SearchTerm::Criteria::NONE;

    SearchTerm::Criteria matchedCriteria = SearchTerm::Criteria::NONE;

    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::DATA_CONTENT)
    {
        matchedCriteria = static_cast<RegFind::SearchTerm::Criteria>(ExactDatas(aTerm, RegValue) | matchedCriteria);
    }

    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::DATA_CONTENT_REGEX)
    {
        matchedCriteria = static_cast<RegFind::SearchTerm::Criteria>(RegexDatas(aTerm, RegValue) | matchedCriteria);
    }

    if (aTerm->m_criteriaRequired
        & (static_cast<SearchTerm::Criteria>(
            SearchTerm::Criteria::DATA_SIZE_EQUAL | SearchTerm::Criteria::DATA_SIZE_LESS
            | SearchTerm::Criteria::DATA_SIZE_MORE)))
    {
        matchedCriteria = static_cast<RegFind::SearchTerm::Criteria>(DatasSizeMatch(aTerm, RegValue) | matchedCriteria);
    }

    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::DATA_CONTAINS)
    {
        matchedCriteria = static_cast<RegFind::SearchTerm::Criteria>(DatasContains(aTerm, RegValue) | matchedCriteria);
    }

    return matchedCriteria;
}

RegFind::SearchTerm::Criteria
RegFind::ValueType_(const std::shared_ptr<RegFind::SearchTerm>& aTerm, const RegistryValue* const RegValue) const
{
    if (RegValue == nullptr)
        return RegFind::SearchTerm::Criteria::NONE;

    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::VALUE_TYPE)
    {
        if (aTerm->m_ValueType == RegValue->GetType())
            return SearchTerm::Criteria::VALUE_TYPE;
    }
    return SearchTerm::Criteria::NONE;
}

RegFind::SearchTerm::Criteria RegFind::MatchValueType(
    const std::shared_ptr<RegFind::SearchTerm>& aTerm,
    RegFind::SearchTerm::Criteria required,
    const RegistryValue* const RegValue) const
{
    DBG_UNREFERENCED_PARAMETER(required);
    if (RegValue == nullptr)
        return RegFind::SearchTerm::Criteria::NONE;

    SearchTerm::Criteria matchedCriteria = SearchTerm::Criteria::NONE;
    if (aTerm->m_criteriaRequired & SearchTerm::Criteria::VALUE_TYPE)
    {
        matchedCriteria = static_cast<RegFind::SearchTerm::Criteria>(ValueType_(aTerm, RegValue) | matchedCriteria);
    }
    return matchedCriteria;
}

RegFind::SearchTerm::Criteria RegFind::LookupSpec(
    const std::shared_ptr<SearchTerm>& aTerm,
    const SearchTerm::Criteria matched,
    std::shared_ptr<Match>& aMatch,
    const RegistryKey* const Regkey) const
{
    SearchTerm::Criteria matchedSpec = matched;
    SearchTerm::Criteria requiredSpec = aTerm->m_criteriaRequired;

    if (aTerm->DependsOnKeyName())
    {
        SearchTerm::Criteria requiredKeyNameSpecs =
            static_cast<SearchTerm::Criteria>(aTerm->m_criteriaRequired & SearchTerm::KeyNameMask());
        SearchTerm::Criteria matchedKeyNameSpecs = MatchKeyName(aTerm, requiredKeyNameSpecs, Regkey);
        if (requiredKeyNameSpecs == matchedKeyNameSpecs)
            matchedSpec = static_cast<SearchTerm::Criteria>(matchedSpec | matchedKeyNameSpecs);
        else
            return SearchTerm::Criteria::NONE;
    }

    if (aTerm->DependsOnKeyPath())
    {
        SearchTerm::Criteria requiredKeyPathSpecs =
            static_cast<SearchTerm::Criteria>(aTerm->m_criteriaRequired & SearchTerm::KeyPathMask());
        SearchTerm::Criteria matchedKeyPathSpecs = MatchKeyPath(aTerm, requiredKeyPathSpecs, Regkey);
        if (requiredKeyPathSpecs == matchedKeyPathSpecs)
            matchedSpec = static_cast<SearchTerm::Criteria>(matchedSpec | matchedKeyPathSpecs);
        else
            return SearchTerm::Criteria::NONE;
    }

    if (matchedSpec == requiredSpec)
    {
        if (aMatch == nullptr)
            aMatch = std::make_shared<Match>(aTerm);

        aMatch->AddKeyNameMatch(Regkey);
        return matchedSpec;
    }

    return RegFind::SearchTerm::Criteria::NONE;
}

RegFind::SearchTerm::Criteria RegFind::LookupSpec(
    const std::shared_ptr<SearchTerm>& aTerm,
    const SearchTerm::Criteria matched,
    std::shared_ptr<Match>& aMatch,
    const RegistryValue* const RegValue) const
{
    SearchTerm::Criteria matchedSpec = matched;
    SearchTerm::Criteria requiredSpec = aTerm->m_criteriaRequired;

    if (aTerm->DependsOnKeyName())
    {
        SearchTerm::Criteria requiredKeyNameSpecs =
            static_cast<SearchTerm::Criteria>(aTerm->m_criteriaRequired & SearchTerm::KeyNameMask());
        SearchTerm::Criteria matchedKeyNameSpecs = MatchKeyName(aTerm, requiredKeyNameSpecs, RegValue->GetParentKey());
        if (requiredKeyNameSpecs == matchedKeyNameSpecs)
            matchedSpec = static_cast<SearchTerm::Criteria>(matchedSpec | matchedKeyNameSpecs);
        else
            return SearchTerm::Criteria::NONE;
    }

    if (aTerm->DependsOnKeyPath())
    {
        SearchTerm::Criteria requiredKeyPathSpecs =
            static_cast<SearchTerm::Criteria>(aTerm->m_criteriaRequired & SearchTerm::KeyPathMask());
        SearchTerm::Criteria matchedKeyPathSpecs = MatchKeyPath(aTerm, requiredKeyPathSpecs, RegValue->GetParentKey());
        if (requiredKeyPathSpecs == matchedKeyPathSpecs)
            matchedSpec = static_cast<SearchTerm::Criteria>(matchedSpec | matchedKeyPathSpecs);
        else
            return SearchTerm::Criteria::NONE;
    }

    if (aTerm->DependsOnValueType())
    {
        SearchTerm::Criteria requiredValueTypeSpecs =
            static_cast<SearchTerm::Criteria>(aTerm->m_criteriaRequired & SearchTerm::ValueTypeMask());
        SearchTerm::Criteria matchedValueTypeSpecs = MatchValueType(aTerm, requiredValueTypeSpecs, RegValue);
        if (requiredValueTypeSpecs == matchedValueTypeSpecs)
            matchedSpec = static_cast<SearchTerm::Criteria>(matchedSpec | matchedValueTypeSpecs);
        else
            return SearchTerm::Criteria::NONE;
    }

    if (aTerm->DependsOnValueName())
    {
        SearchTerm::Criteria requiredValueNameSpecs =
            static_cast<SearchTerm::Criteria>(aTerm->m_criteriaRequired & SearchTerm::ValueNameMask());
        SearchTerm::Criteria matchedValueNameSpecs = MatchValueName(aTerm, requiredValueNameSpecs, RegValue);
        if (requiredValueNameSpecs == matchedValueNameSpecs)
            matchedSpec = static_cast<SearchTerm::Criteria>(matchedSpec | matchedValueNameSpecs);
        else
            return SearchTerm::Criteria::NONE;
    }

    if (aTerm->DependsOnData())
    {
        SearchTerm::Criteria requiredDataSpecs =
            static_cast<SearchTerm::Criteria>(aTerm->m_criteriaRequired & SearchTerm::DataMask());
        SearchTerm::Criteria matchedDataSpecs = MatchDataAndSize(aTerm, requiredDataSpecs, RegValue);
        if (requiredDataSpecs == matchedDataSpecs)
            matchedSpec = static_cast<SearchTerm::Criteria>(matchedSpec | matchedDataSpecs);
        else
            return SearchTerm::Criteria::NONE;
    }

    if (matchedSpec == requiredSpec)
    {
        if (aMatch == nullptr)
            aMatch = std::make_shared<Match>(aTerm);
        aMatch->AddValueNameMatch(RegValue);
        return matchedSpec;
    }

    return RegFind::SearchTerm::Criteria::NONE;
}

const std::vector<std::shared_ptr<RegFind::Match>> RegFind::FindMatch(const RegistryKey* const RegKey)
{
    std::vector<std::shared_ptr<RegFind::Match>> MatchVector;
    std::shared_ptr<RegFind::Match> retval;

    if (!m_ExactKeyNameSpecs.empty())
    {
        const std::string& name = RegKey->GetShortKeyName();
        auto it = m_ExactKeyNameSpecs.find(name);
        while (it != m_ExactKeyNameSpecs.end())
        {
            if (it->second->DependsOnValueOrData())
            {
                it++;
                continue;
            }

            // check if term already matched
            auto term = m_Matches.find(it->second);
            retval = std::make_shared<RegFind::Match>(it->second);
            if (term != m_Matches.end())
            {
                auto matched = LookupSpec(it->second, SearchTerm::Criteria::KEY_NAME, retval, RegKey);
                if (matched != SearchTerm::Criteria::NONE)
                {
                    // Add into this specific key match vector (used by callback)
                    MatchVector.push_back(retval);
                    // Add into the global match vector
                    term->second->AddKeyNameMatch(RegKey);
                }
            }
            // if not, use new match
            else
            {

                auto matched = LookupSpec(it->second, SearchTerm::Criteria::KEY_NAME, retval, RegKey);
                if (matched != SearchTerm::Criteria::NONE)
                {
                    // Add into the global match vector
                    m_Matches.insert(MatchesMap::value_type(it->second, retval));
                    // Add into this specific key match vector (used by callback)
                    MatchVector.push_back(retval);
                }
            }
            it++;
        }
    }

    if (!m_ExactKeyPathSpecs.empty())
    {
        const std::string& name = RegKey->GetKeyName();
        auto it = m_ExactKeyPathSpecs.find(name);
        while (it != m_ExactKeyPathSpecs.end())
        {
            if (it->second->DependsOnValueOrData())
            {
                it++;
                continue;
            }
            // check if term already matched
            auto term = m_Matches.find(it->second);
            retval = std::make_shared<RegFind::Match>(it->second);
            if (term != m_Matches.end())
            {
                auto matched = LookupSpec(it->second, SearchTerm::Criteria::KEY_PATH, retval, RegKey);
                if (matched != SearchTerm::Criteria::NONE)
                {
                    // Add into this specific key match vector (used by callback)
                    MatchVector.push_back(retval);
                    // Add into the global match vector
                    term->second->AddKeyNameMatch(RegKey);
                }
            }
            // if not, use new match
            else
            {

                auto matched = LookupSpec(it->second, SearchTerm::Criteria::KEY_PATH, retval, RegKey);
                if (matched != SearchTerm::Criteria::NONE)
                {
                    // Add into the global match vector
                    m_Matches.insert(MatchesMap::value_type(it->second, retval));
                    // Add into this specific key match vector (used by callback)
                    MatchVector.push_back(retval);
                }
            }
            it++;
        }
    }
    else if (retval != nullptr)
        retval->Reset();

    for (auto term_it = begin(m_Specs); term_it != end(m_Specs); ++term_it)
    {
        if (!(*term_it)->DependsOnValueOrData())
        {
            // check if term already matched
            auto term = m_Matches.find(*term_it);
            std::shared_ptr<RegFind::Match> retval = std::make_shared<RegFind::Match>(*term_it);
            if (term != m_Matches.end())
            {
                auto matched = LookupSpec(*term_it, SearchTerm::Criteria::NONE, retval, RegKey);
                if (matched == (*term_it)->m_criteriaRequired)
                {
                    // Add into this specific key match vector (used by callback)
                    MatchVector.push_back(retval);
                    // Add into the global match vector
                    term->second->AddKeyNameMatch(RegKey);
                }
            }
            // if not, use new match
            else
            {

                auto matched = LookupSpec(*term_it, SearchTerm::Criteria::NONE, retval, RegKey);
                if (matched == (*term_it)->m_criteriaRequired)
                {
                    // Add into the global match vector
                    m_Matches.insert(MatchesMap::value_type(*term_it, retval));
                    // Add into this specific key match vector (used by callback)
                    MatchVector.push_back(retval);
                }
            }
        }
    }

    return MatchVector;
}

const std::vector<std::shared_ptr<RegFind::Match>> RegFind::FindMatch(const RegistryValue* const RegValue)
{
    std::shared_ptr<RegFind::Match> retval;
    const RegistryKey* const pKey = RegValue->GetParentKey();
    std::shared_ptr<RegFind::Match> pRetval;
    std::vector<std::shared_ptr<RegFind::Match>> MatchVector;

    if (!m_ExactKeyNameSpecs.empty())
    {
        const std::string& name = pKey->GetShortKeyName();
        auto it = m_ExactKeyNameSpecs.find(name);
        while (it != m_ExactKeyNameSpecs.end())
        {
            if (!it->second->DependsOnValueOrData())
            {
                it++;
                continue;
            }

            // check if term already matched
            auto term = m_Matches.find(it->second);
            if (term != m_Matches.end())
            {
                auto matched = LookupSpec(it->second, SearchTerm::Criteria::KEY_NAME, retval, RegValue);
                if (matched != SearchTerm::Criteria::NONE)
                {
                    // Add into this specific key match vector (used by callback)
                    MatchVector.push_back(retval);
                    // Add into the global match vector
                    term->second->AddValueNameMatch(RegValue);
                }
            }
            // if not, use new match
            else
            {
                retval = std::make_shared<RegFind::Match>(it->second);
                auto matched = LookupSpec(it->second, SearchTerm::Criteria::KEY_NAME, retval, RegValue);
                if (matched != SearchTerm::Criteria::NONE)
                {
                    m_Matches.insert(MatchesMap::value_type(it->second, retval));
                    MatchVector.push_back(retval);
                }
            }
            it++;
        }
    }

    if (!m_ExactKeyPathSpecs.empty())
    {
        const std::string& name = pKey->GetKeyName();
        auto it = m_ExactKeyPathSpecs.find(name);
        while (it != m_ExactKeyPathSpecs.end())
        {
            if (!it->second->DependsOnValueOrData())
            {
                it = next(it);
                continue;
            }
            // check if term already matched
            auto term = m_Matches.find(it->second);
            if (term != m_Matches.end())
            {
                auto matched = LookupSpec(it->second, SearchTerm::Criteria::KEY_PATH, retval, RegValue);
                if (matched != SearchTerm::Criteria::NONE)
                {
                    // Add into this specific key match vector (used by callback)
                    MatchVector.push_back(retval);
                    // Add into the global match vector
                    term->second->AddValueNameMatch(RegValue);
                }
            }
            // if not, use new match
            else
            {
                retval = std::make_shared<RegFind::Match>(it->second);
                auto matched = LookupSpec(it->second, SearchTerm::Criteria::KEY_PATH, retval, RegValue);
                if (matched != SearchTerm::Criteria::NONE)
                {
                    m_Matches.insert(MatchesMap::value_type(it->second, retval));
                    MatchVector.push_back(retval);
                }
            }
            it = next(it);
        }
    }

    std::string name = RegValue->GetValueName();
    if (!m_ExactValueNameSpecs.empty())
    {
        auto it = m_ExactValueNameSpecs.find(name);
        while (it != m_ExactValueNameSpecs.end())
        {
            if (!it->second->DependsOnValueOrData())
            {
                it++;
                continue;
            }

            // check if term already matched
            auto term = m_Matches.find(it->second);
            if (term != m_Matches.end())
            {
                auto matched = LookupSpec(it->second, SearchTerm::Criteria::VALUE_NAME, retval, RegValue);
                if (matched != SearchTerm::Criteria::NONE)
                {
                    // Add into this specific key match vector (used by callback)
                    MatchVector.push_back(retval);
                    // Add into the global match vector
                    term->second->AddValueNameMatch(RegValue);
                }
            }
            // if not, use new match
            else
            {
                retval = std::make_shared<RegFind::Match>(it->second);
                auto matched = LookupSpec(it->second, SearchTerm::Criteria::VALUE_NAME, retval, RegValue);
                if (matched != SearchTerm::Criteria::NONE)
                {
                    m_Matches.insert(MatchesMap::value_type(it->second, retval));
                    MatchVector.push_back(retval);
                }
            }
            it++;
        }
    }

    for (auto term_it = begin(m_Specs); term_it != end(m_Specs); ++term_it)
    {
        if (!(*term_it)->DependsOnValueOrData())
        {
            continue;
        }

        else
        {
            // check if term already matched
            auto term = m_Matches.find(*term_it);
            if (term != m_Matches.end())
            {
                auto matched = LookupSpec(*term_it, SearchTerm::Criteria::NONE, retval, RegValue);
                if (matched == (*term_it)->m_criteriaRequired)
                {
                    // Add into this specific key match vector (used by callback)
                    MatchVector.push_back(retval);
                    // Add into the global match vector
                    term->second->AddValueNameMatch(RegValue);
                }
            }
            // if not, use new match
            else
            {
                retval = std::make_shared<RegFind::Match>(*term_it);
                auto matched = LookupSpec(*term_it, SearchTerm::Criteria::NONE, retval, RegValue);
                if (matched == (*term_it)->m_criteriaRequired)
                {
                    m_Matches.insert(MatchesMap::value_type(*term_it, retval));
                    MatchVector.push_back(retval);
                }
            }
        }
    }
    return MatchVector;
}

HRESULT RegFind::Find(
    const std::shared_ptr<ByteStream>& location,
    FoundKeyMatchCallback aKeyCallback,
    FoundValueMatchCallback aValueCallback)
{

    ClearMatches();

    HRESULT hr = S_OK;

    if (location != nullptr)
    {
        RegistryHive Hive(_L_);
        hr = Hive.LoadHive(*location);
        if (hr != S_OK)
        {
            log::Error(_L_, hr, L"RegFind::Find : can't load hive.\r\n");
            return hr;
        }

        std::function<void(const RegistryKey* const)> CallbackOnKey = [this,
                                                                       aKeyCallback](const RegistryKey* const RegKey) {
            std::vector<std::shared_ptr<RegFind::Match>> result = FindMatch(RegKey);
            if ((aKeyCallback != nullptr) && (!result.empty()))
                aKeyCallback(result);
        };

        std::function<void(const RegistryValue* const)> CallBackOnValue =
            [this, aValueCallback](const RegistryValue* const RegValue) {
                std::vector<std::shared_ptr<RegFind::Match>> result = FindMatch(RegValue);
                if ((aValueCallback != nullptr) && (!result.empty()))
                    aValueCallback(result);
            };

        if (FAILED(hr = Hive.Walk(CallbackOnKey, CallBackOnValue)))
        {
            log::Error(_L_, hr, L"RegFind::Find : can't walk hive.\r\n");
            return hr;
        }
    }
    else
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER),
            L"RegFind::Find : a search location is required.\r\n");
        return hr;
    }

    log::Verbose(_L_, L"Done!\r\n");
    return hr;
}
