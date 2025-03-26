//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "Winternl.h"

#include "ConfigItem.h"
#include "ConfigFile.h"
#include "ConfigFileReader.h"

#include "EmbeddedResource.h"

#include "MemoryStream.h"

#include "ParameterCheck.h"

#include "FileFind.h"

#include <xmllite.h>
#include <filesystem>

#include "Log/Log.h"

#ifndef STATUS_SUCCESS
#    define STATUS_SUCCESS (0)
#endif

namespace fs = std::filesystem;
using namespace std;

using namespace Orc;

ConfigFile::ConfigFile()
{
    m_hHeap = HeapCreate(HEAP_GENERATE_EXCEPTIONS, 0L, 0L);
}

HRESULT ConfigFile::TrimString(struct _UNICODE_STRING* pString)
{
    DWORD dwDecal_L = 0, dwDecal_R = 0;
    if (pString == NULL)
        return E_POINTER;
    for (unsigned int i = 0; i < (pString->Length / sizeof(WCHAR)); i++)
    {
        if (iswspace(pString->Buffer[i]))
            dwDecal_L++;
        else
            break;
    }

    for (unsigned int i = (pString->Length / sizeof(WCHAR)) - 2; i >= dwDecal_L; i--)
    {
        if (iswspace(pString->Buffer[i]))
            dwDecal_R++;
        else
            break;
    }
    if (dwDecal_L)
        wcsncpy_s(
            pString->Buffer,
            pString->MaximumLength / sizeof(WCHAR),
            pString->Buffer + dwDecal_L,
            (pString->Length / sizeof(WCHAR)) - dwDecal_L);

    if (dwDecal_R + dwDecal_L)
    {
        pString->Buffer[(pString->Length / sizeof(WCHAR)) - (dwDecal_R + dwDecal_L + 1)] = L'\0';
    }
    pString->Length -= (USHORT)(dwDecal_R + dwDecal_L) * sizeof(WCHAR);
    return S_OK;
}

HRESULT ConfigFile::TrimString(std::wstring&)
{
    return E_NOTIMPL;
}

HRESULT ConfigFile::SetData(ConfigItem& config, const UNICODE_STRING& String)
{
    config.strData.assign(String.Buffer, String.Length / sizeof(WCHAR));
    config.Status = ConfigItem::PRESENT;
    return S_OK;
}

HRESULT ConfigFile::SetData(ConfigItem& config, const std::wstring& aString)
{
    config.strData = aString;
    return S_OK;
}

HRESULT ConfigFile::LookupAndReadConfiguration(
    int argc,
    LPCWSTR argv[],
    ConfigFileReader& r,
    LPCWSTR szConfigOpt,
    LPCWSTR szDefaultConfigResource,
    LPCWSTR szReferenceConfigResource,
    LPCWSTR szCompanionExtension,
    ConfigItem& Config)
{
    HRESULT hr = E_FAIL;

    wstring strConfigFile;
    wstring strConfigResource;

    try
    {

        for (int i = 0; i < argc; i++)
        {
            switch (argv[i][0])
            {
                case L'/':
                case L'-':
                    if (szConfigOpt != nullptr && !_wcsnicmp(argv[i] + 1, szConfigOpt, wcslen(szConfigOpt)))
                    {
                        LPCWSTR pEquals = wcschr(argv[i], L'=');
                        if (!pEquals)
                        {
                            Log::Critical(
                                L"Option /{} should be like: /{}=c:\\temp\\config.xml or /{}=res:#myconfig",
                                szConfigOpt,
                                szConfigOpt,
                                szConfigOpt);
                            return E_FAIL;
                        }
                        else
                        {
                            if (FAILED(hr = ::ExpandFilePath(pEquals + 1, strConfigFile)))
                            {
                                strConfigResource = pEquals + 1;
                                if (EmbeddedResource::IsResourceBased(strConfigResource))
                                {
                                    // Config is in a resource
                                    Log::Debug(L"Specified configuration is a resource");
                                    hr = S_OK;
                                }
                                else
                                {
                                    Log::Error(
                                        L"Invalid config file specified: '{}' [{}]", pEquals + 1, SystemError(hr));
                                    return hr;
                                }
                            }
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }
    catch (...)
    {
        Log::Error(L"Failed during argument parsing, exiting");
        return E_FAIL;
    }

    if (szCompanionExtension != nullptr && strConfigFile.empty())
    {
        Log::Debug(L"Try to locate configuration from companion extension '{}'", szCompanionExtension);

        if (szReferenceConfigResource != NULL)
        {
            HMODULE hMod = NULL;
            HRSRC hRes = NULL;
            std::wstring strBinaryPath;

            std::wstring strBinary, strResName, strNameInArchive, strFormat;

            if (FAILED(
                    hr = EmbeddedResource::SplitResourceReference(
                        szReferenceConfigResource, strBinary, strResName, strNameInArchive, strFormat)))
            {
                Log::Warn(
                    L"Failed to locate '{}' value as a reference for local resource lookup [{}]",
                    szReferenceConfigResource,
                    SystemError(hr));
            }

            if (FAILED(
                    hr = EmbeddedResource::LocateResource(strBinary, strResName, L"BINARY", hMod, hRes, strBinaryPath)))
            {
                Log::Warn(
                    L"Failed to locate '{}' value as a reference for local resource lookup [{}]",
                    szReferenceConfigResource,
                    SystemError(hr));
            }
            else
            {
                fs::path config_path(strBinaryPath);
                config_path.replace_extension(szCompanionExtension);

                if (SUCCEEDED(VerifyFileExists(config_path.c_str())))
                {
                    strConfigFile = config_path.wstring();
                }
            }
        }
        else
        {
            // Companion configuration
            WCHAR szEXEPath[ORC_MAX_PATH];
            if (GetModuleFileName(NULL, szEXEPath, ORC_MAX_PATH))
            {
                fs::path config_path(szEXEPath);

                config_path.replace_extension(szCompanionExtension);

                if (SUCCEEDED(VerifyFileExists(config_path.c_str())))
                {
                    strConfigFile = config_path.wstring();
                }
            }
        }
    }

    if (!strConfigFile.empty())
    {
        Log::Debug(L"Load configuration from file: '{}'", strConfigResource);

        // Config file is used, let's read it
        if (FAILED(hr = r.ReadConfig(strConfigFile.c_str(), Config)))
        {
            Log::Error(L"Failed to read config file '{}' [{}]", strConfigFile, SystemError(hr));
            return hr;
        }

        if (FAILED(hr = r.CheckConfig(Config)))
        {
            Log::Error(L"Config file '{}' is incorrect and cannot be used [{}]", strConfigFile, SystemError(hr));
            return hr;
        }

        Log::Debug(L"Configuration loaded from file: '{}'", strConfigResource);
    }
    else if (!strConfigResource.empty())
    {
        Log::Debug(L"Load configuration from resource: '{}'", strConfigResource);

        CBinaryBuffer buffer;
        if (SUCCEEDED(hr = EmbeddedResource::ExtractToBuffer(strConfigResource, buffer)))
        {
            auto memstream = std::make_shared<MemoryStream>();

            if (FAILED(hr = memstream->OpenForReadOnly(buffer.GetData(), buffer.GetCount())))
            {
                Log::Error(
                    L"Failed to create stream for config resource '{}' [{}]", strConfigResource, SystemError(hr));
                return hr;
            }

            // Config file is used, let's read it
            if (FAILED(hr = r.ReadConfig(memstream, Config)))
            {
                Log::Error(L"Failed to read config resource '{}' [{}]", strConfigResource, SystemError(hr));
                return hr;
            }

            if (FAILED(hr = r.CheckConfig(Config)))
            {
                Log::Error(
                    L"Config resource '{}' is incorrect and cannot be used [{}]", strConfigResource, SystemError(hr));
                return hr;
            }

            Log::Debug(L"Default configuration loaded from resource: '{}'", strConfigResource);
        }
        else
        {
            Log::Error(
                L"WARNING: Configuration could not be loaded from resource '{}' [{}]",
                strConfigResource,
                SystemError(hr));
        }
    }
    else if (szDefaultConfigResource != nullptr)
    {
        wstring strConfigRef(szDefaultConfigResource);

        Log::Debug(L"Try to load default configuration from: '{}'", strConfigRef);

        CBinaryBuffer buffer;
        if (SUCCEEDED(hr = EmbeddedResource::ExtractToBuffer(strConfigRef, buffer)))
        {
            // Config file is used, let's read it
            auto memstream = std::make_shared<MemoryStream>();

            if (FAILED(hr = memstream->OpenForReadOnly(buffer.GetData(), buffer.GetCount())))
            {
                Log::Error(L"Failed to create stream for config resource '{}' [{}]", strConfigRef, SystemError(hr));
                return hr;
            }

            if (FAILED(hr = r.ReadConfig(memstream, Config)))
            {
                Log::Error(L"Failed to read config resource '{}' [{}]", strConfigRef, SystemError(hr));
                return hr;
            }

            if (FAILED(hr = r.CheckConfig(Config)))
            {
                Log::Error(L"Config resource '{}' is incorrect and cannot be used [{}]", strConfigRef, SystemError(hr));
                return hr;
            }

            Log::Debug(L"Default configuration loaded from: '{}'", strConfigRef);
        }
        else
        {
            Log::Debug(
                L"WARNING: Configuration could not be loaded from resource '{}' [{}]", strConfigRef, SystemError(hr));
        }
    }
    return S_OK;
}

HRESULT ConfigFile::CheckConfig(const ConfigItem& config)
{

    if (config.Flags == ConfigItem::MANDATORY && !config)
    {
        Log::Error(L"Element '{}' is mandatory and missing", config.strName);
        return E_FAIL;
    }
    HRESULT hr = S_OK;

    if (config)
    {
        switch (config.Type)
        {
            case ConfigItem::ATTRIBUTE:
                break;
            case ConfigItem::NODE:
                std::for_each(begin(config.SubItems), end(config.SubItems), [&hr](const ConfigItem& item) {
                    HRESULT subhr = CheckConfig(item);
                    if (FAILED(subhr))
                        hr = subhr;
                });
                break;
            case ConfigItem::NODELIST:
                std::for_each(begin(config.NodeList), end(config.NodeList), [&hr](const ConfigItem& item) {
                    HRESULT subhr = CheckConfig(item);
                    if (FAILED(subhr))
                        hr = subhr;
                });
                break;
        }
    }
    return hr;
}

HRESULT ConfigFile::PrintConfig(const ConfigItem& config, DWORD dwIndent)
{
    const auto MAX_INDENT = 20;

    WCHAR szIndent[MAX_INDENT];
    if (dwIndent >= MAX_INDENT)
        return E_INVALIDARG;
    wmemset(szIndent, L'\t', MAX_INDENT);
    szIndent[dwIndent] = L'\0';

    switch (config.Type)
    {
        case ConfigItem::NODE:
            Log::Info(
                L"{}NODE: \"{}\" {} {}",
                szIndent,
                config.strName,
                (config.Flags & ConfigItem::MANDATORY) ? L"Mandatory" : L"Optional",
                config ? L"Present" : L"Absent");
            if (!config.empty())
                Log::Info(L"{}\tDATA: \"{}\" ", szIndent, config.c_str());

            std::for_each(begin(config.SubItems), end(config.SubItems), [dwIndent](const ConfigItem& item) {
                PrintConfig(item, dwIndent + 1);
            });
            break;

        case ConfigItem::ATTRIBUTE:
            Log::Info(
                L"{}ATTRIBUTE: \"{}={}\" {} {}",
                szIndent,
                config.strName,
                config.c_str(),
                (config.Flags & ConfigItem::MANDATORY) ? L"Mandatory" : L"Optional",
                config ? L"Present" : L"Absent");
            break;
        case ConfigItem::NODELIST: {
            Log::Info(
                L"{}NODES: \"{}\" {} {}",
                szIndent,
                config.strName,
                (config.Flags & ConfigItem::MANDATORY) ? L"Mandatory" : L"Optional",
                config ? L"Present" : L"Absent");

            std::for_each(begin(config.NodeList), end(config.NodeList), [dwIndent](const ConfigItem& item) {
                PrintConfig(item, dwIndent + 1);
            });
        }
        break;
    }

    return S_OK;
}

HRESULT ConfigFile::SetOutputSpec(ConfigItem& item, const OutputSpec& outputSpec)
{

    switch (outputSpec.Type)
    {
        case OutputSpec::Kind::File:
            item.strData = outputSpec.Path;
            item.Status = ConfigItem::PRESENT;
            break;
        case OutputSpec::Kind::Archive:
            item.strData = outputSpec.Path;
            if (outputSpec.ArchiveFormat != ArchiveFormat::Unknown)
            {
                item.SubItems[CONFIG_OUTPUT_FORMAT].strData =
                    OrcArchive::GetArchiveFormatString(outputSpec.ArchiveFormat);
                item.SubItems[CONFIG_OUTPUT_FORMAT].Status = ConfigItem::PRESENT;
            }
            if (!outputSpec.Compression.empty())
            {
                item.SubItems[CONFIG_OUTPUT_COMPRESSION].strData = outputSpec.Compression;
                item.SubItems[CONFIG_OUTPUT_COMPRESSION].Status = ConfigItem::PRESENT;
            }
            if (!outputSpec.Password.empty())
            {
                item.SubItems[CONFIG_OUTPUT_PASSWORD].strData = outputSpec.Password;
                item.SubItems[CONFIG_OUTPUT_PASSWORD].Status = ConfigItem::PRESENT;
            }
            item.Status = ConfigItem::PRESENT;
            break;
        case OutputSpec::Kind::TableFile:
            return E_NOTIMPL;
    }

    DBG_UNREFERENCED_PARAMETER(outputSpec);
    DBG_UNREFERENCED_PARAMETER(item);
    return S_OK;
}

HRESULT ConfigFile::GetOutputDir(const ConfigItem& item, std::wstring& outputDir, OutputSpec::Encoding& anEncoding)
{
    HRESULT hr = E_FAIL;

    if (item)
    {
        if (FAILED(hr = ::GetOutputDir(item.c_str(), outputDir)))
        {
            Log::Error(L"Error in specified outputdir '{}' in config file [{}]", item.c_str(), SystemError(hr));
            return hr;
        }

        anEncoding = OutputSpec::Encoding::UTF8;
        if (item.SubItems.size() > CONFIG_CSVENCODING)
        {
            if (item.SubItems[CONFIG_CSVENCODING])
            {
                if (!_wcsnicmp(item.SubItems[CONFIG_CSVENCODING].c_str(), L"utf8", wcslen(L"utf8")))
                {
                    anEncoding = OutputSpec::Encoding::UTF8;
                }
                else if (!_wcsnicmp(item.SubItems[CONFIG_CSVENCODING].c_str(), L"utf16", wcslen(L"utf16")))
                {
                    anEncoding = OutputSpec::Encoding::UTF16;
                }
                else
                {
                    Log::Error(
                        L"Invalid encoding for outputdir in config file: '{}' [{}]",
                        item.SubItems[CONFIG_CSVENCODING].c_str(),
                        SystemError(hr));
                    return hr;
                }
            }
        }
    }

    return S_OK;
}

HRESULT ConfigFile::SetOutputDir(ConfigItem& item, const std::wstring& outputDir, OutputSpec::Encoding anEncoding)
{
    item.strData = outputDir;

    switch (anEncoding)
    {
        case OutputSpec::Encoding::UTF8:
            item.SubItems[CONFIG_CSVENCODING].strData = L"utf8";
            item.SubItems[CONFIG_CSVENCODING].Status = ConfigItem::PRESENT;
            break;
        case OutputSpec::Encoding::UTF16:
            item.SubItems[CONFIG_CSVENCODING].strData = L"utf16";
            item.SubItems[CONFIG_CSVENCODING].Status = ConfigItem::PRESENT;
            break;
        default:
            break;
    }

    item.Status = ConfigItem::PRESENT;
    return S_OK;
}

HRESULT ConfigFile::GetOutputFile(const ConfigItem& item, std::wstring& outputFile, OutputSpec::Encoding& anEncoding)
{
    HRESULT hr = E_FAIL;

    if (item)
    {
        if (FAILED(hr = ::GetOutputFile(item.c_str(), outputFile)))
        {
            Log::Error(L"Error in specified output file in config file [{}]", SystemError(hr));
            return hr;
        }
        anEncoding = OutputSpec::Encoding::UTF8;
        if (item.SubItems.size() > CONFIG_CSVENCODING
            && !_wcsicmp(item.SubItems[CONFIG_CSVENCODING].strName.c_str(), L"encoding"))
        {
            if (item.SubItems[CONFIG_CSVENCODING])
            {
                if (!_wcsnicmp(item.SubItems[CONFIG_CSVENCODING].c_str(), L"utf8", wcslen(L"utf8")))
                {
                    anEncoding = OutputSpec::Encoding::UTF8;
                }
                else if (!_wcsnicmp(item.SubItems[CONFIG_CSVENCODING].c_str(), L"utf16", wcslen(L"utf16")))
                {
                    anEncoding = OutputSpec::Encoding::UTF16;
                }
                else
                {
                    Log::Error(
                        L"Invalid encoding for outputcab in config file: '{}' [{}]",
                        item.SubItems[CONFIG_CSVENCODING].c_str(),
                        SystemError(hr));
                    return hr;
                }
            }
        }
    }
    return S_OK;
}

HRESULT ConfigFile::SetOutputFile(ConfigItem& item, const std::wstring& outputFile, OutputSpec::Encoding anEncoding)
{
    item.strData = outputFile;
    item.Status = ConfigItem::PRESENT;

    switch (anEncoding)
    {
        case OutputSpec::Encoding::UTF8:
            item.SubItems[CONFIG_CSVENCODING].strData = L"utf8";
            item.SubItems[CONFIG_CSVENCODING].Status = ConfigItem::PRESENT;
            break;
        case OutputSpec::Encoding::UTF16:
            item.SubItems[CONFIG_CSVENCODING].strData = L"utf16";
            item.SubItems[CONFIG_CSVENCODING].Status = ConfigItem::PRESENT;
            break;
        default:
            break;
    }
    return S_OK;
}

HRESULT ConfigFile::GetInputFile(const ConfigItem& item, std::wstring& inputFile)
{
    HRESULT hr = E_FAIL;

    if (item)
    {
        if (FAILED(hr = ::ExpandFilePath(item.c_str(), inputFile)))
        {
            Log::Error(L"Error in specified inputfile in config file '{}' [{}]", item.c_str(), SystemError(hr));
            return hr;
        }
    }
    return S_OK;
}

HRESULT ConfigFile::SetInputFile(ConfigItem& item, const std::wstring& inputFile)
{
    item.strData = inputFile;
    item.Status = ConfigItem::PRESENT;
    return S_OK;
}

HRESULT ConfigFile::GetSQLConnectionString(const ConfigItem& item, std::wstring& strConnectionString)
{
    if (item.SubItems[CONFIG_SQL_CONNECTIONSTRING])
    {
        strConnectionString = item.SubItems[CONFIG_SQL_CONNECTIONSTRING].strData;
    }
    return S_OK;
}

HRESULT ConfigFile::GetSQLTableName(const ConfigItem& item, const LPWSTR szTableKey, std::wstring& strTableName)
{
    const ConfigItem& tables = item.SubItems[CONFIG_SQL_TABLE];

    if (tables && tables.Type == ConfigItem::NODELIST)
    {
        auto it = begin(tables.NodeList);

        if (szTableKey == nullptr && tables.NodeList.size() > 1)
        {

            for (; it != end(tables.NodeList); ++it)
            {
                if (it->SubItems[CONFIG_SQL_TABLEKEY].Status == ConfigItem::MISSING)
                {
                    // we stop at the very first table found with no name
                    break;
                }
            }
            if (it != end(tables.NodeList))
            {
                Log::Error("While loading schema: No table name specified and none of the table schemas have no name");
                return E_INVALIDARG;
            }
        }
        else if (szTableKey == nullptr)
        {
            // we're ok with first element
        }
        else
        {
            for (; it != end(tables.NodeList); ++it)
            {
                if (it->SubItems[CONFIG_SQL_TABLEKEY])
                {
                    if (!_wcsicmp(it->SubItems[CONFIG_SQL_TABLEKEY].c_str(), szTableKey))
                    {
                        // we stop at the very first table found with the required name
                        break;
                    }
                }
            }
            if (it == end(tables.NodeList))
            {
                Log::Error(L"No table name matches required key ({})", szTableKey);
                return E_INVALIDARG;
            }
        }
        strTableName = it->strData;
    }
    return S_OK;
}

HRESULT ConfigFile::AddSamplePath(ConfigItem& item, const std::wstring& samplePath)
{
    ConfigItem sample;
    sample = item;
    sample.Status = ConfigItem::PRESENT;
    sample.Type = ConfigItem::NODE;
    item.NodeList.push_back(std::move(sample));

    ConfigItem& sampleitem = item.NodeList.back();

    FileFind::SearchTerm aTerm;

    ConfigItem ntfs_find;
    ntfs_find = sampleitem.SubItems[CONFIG_SAMPLE_FILEFIND];
    ntfs_find.Type = ConfigItem::NODE;
    ntfs_find.Status = ConfigItem::PRESENT;

    ntfs_find.SubItems[CONFIG_FILEFIND_PATH].strData = samplePath;
    ntfs_find.SubItems[CONFIG_FILEFIND_PATH].Status = ConfigItem::PRESENT;

    sampleitem.SubItems[CONFIG_SAMPLE_FILEFIND].NodeList.push_back(ntfs_find);
    sampleitem.SubItems[CONFIG_SAMPLE_FILEFIND].Status = ConfigItem::PRESENT;

    item.Status = ConfigItem::PRESENT;
    return S_OK;
}

HRESULT ConfigFile::AddSampleName(ConfigItem& item, const std::wstring& sampleName)
{
    ConfigItem sample;
    sample = item;
    sample.Status = ConfigItem::PRESENT;
    sample.Type = ConfigItem::NODE;
    item.NodeList.push_back(std::move(sample));

    ConfigItem& sampleitem = item.NodeList.back();

    ConfigItem ntfs_find;
    ntfs_find = sampleitem.SubItems[CONFIG_SAMPLE_FILEFIND];
    ntfs_find.Type = ConfigItem::NODE;
    ntfs_find.Status = ConfigItem::PRESENT;

    ntfs_find.SubItems[CONFIG_FILEFIND_NAME].strData = sampleName;
    ntfs_find.SubItems[CONFIG_FILEFIND_NAME].Status = ConfigItem::PRESENT;

    sampleitem.SubItems[CONFIG_SAMPLE_FILEFIND].NodeList.push_back(ntfs_find);
    sampleitem.SubItems[CONFIG_SAMPLE_FILEFIND].Status = ConfigItem::PRESENT;

    item.Status = ConfigItem::PRESENT;
    return S_OK;
}

HRESULT ConfigFile::SetLocations(ConfigItem& item, const std::vector<std::shared_ptr<Location>>& locs)
{
    if (item.Type != ConfigItem::NODELIST)
        return E_INVALIDARG;

    for (const auto& aloc : locs)
    {
        if (aloc != nullptr)
        {
            ConfigItem& RefToItem = item;

            if (aloc->GetParse())
            {
                auto subdirs = aloc->GetSubDirs();
                auto location = aloc->GetLocation();

                if (subdirs.empty())
                {
                    ConfigItem newitem = RefToItem;
                    newitem.Type = ConfigItem::NODE;
                    newitem.strData = location;
                    newitem.Status = ConfigItem::PRESENT;

                    RefToItem.NodeList.push_back(newitem);
                }
                else
                {
                    std::for_each(begin(subdirs), end(subdirs), [&RefToItem, location](const wstring& dir) {
                        ConfigItem newitem = RefToItem;
                        newitem.Type = ConfigItem::NODE;

                        newitem.strData = location;

                        if (location.back() == L'\\' && dir.front() == L'\\')
                            newitem.strData.pop_back();

                        newitem.strData += dir;
                        newitem.Status = ConfigItem::PRESENT;

                        RefToItem.NodeList.push_back(newitem);
                    });
                }
            }
        }
    }

    item.Status = ConfigItem::PRESENT;
    return S_OK;
}

HRESULT ConfigFile::DestroyConfig(ConfigItem& config, bool bDeleteSubItems)
{
    config.strName.clear();
    config.strData.clear();
    if (bDeleteSubItems)
    {
        config.NodeList.clear();
        config.SubItems.clear();
    }
    config.Status = ConfigItem::MISSING;

    return S_OK;
}
