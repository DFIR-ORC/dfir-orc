//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#include "stdafx.h"

#include "FatInfo.h"
#include "FatFileInfo.h"
#include "ConfigFile_FatInfo.h"
#include "FileInfoCommon.h"
#include "Temporary.h"
#include "SystemDetails.h"

using namespace Orc;
using namespace Orc::Command::FatInfo;

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return Orc::Config::FatInfo::root;
}

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaitem)
{
    m_Config.output.Schema = TableOutput::GetColumnsFromConfig(
        m_Config.output.TableKey.empty() ? L"fileinfo" : m_Config.output.TableKey.c_str(), schemaitem);
    m_Config.volumesStatsOutput.Schema = TableOutput::GetColumnsFromConfig(
        m_Config.volumesStatsOutput.TableKey.empty() ? L"volstats" : m_Config.volumesStatsOutput.TableKey.c_str(),
        schemaitem);
    return S_OK;
}

HRESULT Main::GetColumnsAndFiltersFromConfig(const ConfigItem& configItem)
{
    HRESULT hr = E_FAIL;

    const ConfigItem& configitem = configItem[FATINFO_COLUMNS];

    std::for_each(
        begin(configitem[DEFAULT].NodeList), end(configitem[DEFAULT].NodeList), [this](const ConfigItem& item) {
            if (item)
                m_Config.DefaultIntentions = static_cast<Intentions>(
                    m_Config.DefaultIntentions
                    | FileInfo::GetIntentions(
                        item.c_str(), FatFileInfo::g_FatAliasNames, FatFileInfo::g_FatColumnNames));
        });

    std::for_each(begin(configitem[ADD].NodeList), end(configitem[ADD].NodeList), [this](const ConfigItem& item) {
        HRESULT hr1 = E_FAIL;
        Filter filter;
        filter.bInclude = true;

        if (FAILED(
                hr1 = FileInfoCommon::GetFilterFromConfig(
                    item, filter, FatFileInfo::g_FatAliasNames, FatFileInfo::g_FatColumnNames)))
            return;
        else
        {
            if (hr1 == S_OK)
            {
                m_Config.Filters.push_back(filter);
            }
            else if (hr1 == S_FALSE)
            {
                m_Config.ColumnIntentions = static_cast<Intentions>(m_Config.ColumnIntentions | filter.intent);
            }
        }
    });

    std::for_each(begin(configitem[OMIT].NodeList), end(configitem[OMIT].NodeList), [this](const ConfigItem& item) {
        HRESULT hr1 = E_FAIL;
        Filter filter;
        filter.bInclude = false;

        hr1 = FileInfoCommon::GetFilterFromConfig(
            item, filter, FatFileInfo::g_FatAliasNames, FatFileInfo::g_FatColumnNames);

        if (FAILED(hr1))
            return;
        else
        {
            if (hr1 == S_OK)
            {
                m_Config.Filters.push_back(filter);
            }
            else if (hr1 == S_FALSE)
            {
                m_Config.ColumnIntentions =
                    static_cast<Intentions>(m_Config.ColumnIntentions & static_cast<Intentions>(~filter.intent));
            }
        }
    });

    return S_OK;
}

HRESULT Main::GetConfigurationFromConfig(const ConfigItem& configitem)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Config.output.Configure(configitem[FATINFO_OUTPUT])))
    {
        Log::Error("Invalid output specified");
        return hr;
    }

    if (configitem[FATINFO_COMPUTER])
        m_utilitiesConfig.strComputerName = configitem[FATINFO_COMPUTER];

    m_Config.bResurrectRecords = GetResurrectFromConfig(configitem);
    m_Config.bPopSystemObjects = GetPopulateSystemObjectsFromConfig(configitem);

    if (boost::logic::indeterminate(m_Config.bPopSystemObjects))
        m_Config.bPopSystemObjects = false;
    m_Config.locs.SetPopulateSystemObjects((bool)m_Config.bPopSystemObjects);

    if (FAILED(hr = m_Config.locs.AddLocationsFromConfigItem(configitem[FATINFO_LOCATIONS])))
    {
        Log::Error("Failed to get locations definition from config (code: {:#x})", hr);
        return hr;
    }

    if (FAILED(hr = GetColumnsAndFiltersFromConfig(configitem)))
    {
        Log::Error("Failed to get column definition from config (code: {:#x})", hr);
        return hr;
    }

    return S_OK;
}

boost::logic::tribool Main::GetResurrectFromConfig(const ConfigItem& config)
{
    if (config[FATINFO_RESURRECT])
    {
        if (!_wcsnicmp(config[FATINFO_RESURRECT].c_str(), L"no", wcslen(L"no")))
            return false;
        else
            return true;
    }
    return boost::logic::indeterminate;
}

boost::logic::tribool Main::GetPopulateSystemObjectsFromConfig(const ConfigItem& config)
{
    if (config[FATINFO_POP_SYS_OBJ])
    {
        if (!_wcsnicmp(config[FATINFO_POP_SYS_OBJ].c_str(), L"no", wcslen(L"no")))
            return false;
        else
            return true;
    }
    return boost::logic::indeterminate;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
    HRESULT hr = E_FAIL;

    bool bBool = false;
    for (int i = 1; i < argc; i++)
    {
        switch (argv[i][0])
        {
            case L'/':
            case L'-':
                if (OutputOption(argv[i] + 1, L"out", m_Config.output))
                    ;
                else if (ParameterOption(argv[i] + 1, L"Computer", m_utilitiesConfig.strComputerName))
                    ;
                else if (ParameterOption(argv[i] + 1, L"ResurrectRecords", m_Config.bResurrectRecords))
                    ;
                else if (ParameterOption(argv[i] + 1, L"PopSysObj", m_Config.bPopSystemObjects))
                    ;
                else if (EncodingOption(argv[i] + 1, m_Config.output.OutputEncoding))
                    ;
                else if (AltitudeOption(argv[i] + 1, L"Altitude", m_Config.locs.GetAltitude()))
                    ;
                else if (ProcessPriorityOption(argv[i] + 1))
                    ;
                else if (UsageOption(argv[i] + 1))
                    ;
                else if (IgnoreCommonOptions(argv[i] + 1))
                    ;
                else
                {
                    // Here we have the selected default intentions tag

                    LPCWSTR pCur = argv[i] + 1;

                    while (pCur)
                    {
                        LPWSTR pNext = wcschr((LPWSTR)pCur, L',');
                        if (pNext)
                        {
                            *pNext = L'\0';
                            m_Config.DefaultIntentions = static_cast<Intentions>(
                                m_Config.DefaultIntentions
                                | FatFileInfo::GetIntentions(
                                    pCur, FatFileInfo::g_FatAliasNames, FatFileInfo::g_FatColumnNames));
                            pCur = pNext + 1;
                        }
                        else
                        {
                            m_Config.DefaultIntentions = static_cast<Intentions>(
                                m_Config.DefaultIntentions
                                | FatFileInfo::GetIntentions(
                                    pCur, FatFileInfo::g_FatAliasNames, FatFileInfo::g_FatColumnNames));
                            pCur = NULL;
                        }
                    }
                }
                break;

            default:
                break;
        }
    }

    if (boost::logic::indeterminate(m_Config.bPopSystemObjects))
        m_Config.bPopSystemObjects = false;
    m_Config.locs.SetPopulateSystemObjects((bool)m_Config.bPopSystemObjects);

    if (FAILED(hr = m_Config.locs.AddLocationsFromArgcArgv(argc, argv)))
        return hr;

    if (FAILED(
            hr = FileInfoCommon::GetFiltersFromArgcArgv(
                argc, argv, m_Config.Filters, FatFileInfo::g_FatAliasNames, FatFileInfo::g_FatColumnNames)))
        return hr;

    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    HRESULT hr = E_FAIL;

    if (m_utilitiesConfig.strComputerName.empty())
    {
        SystemDetails::GetOrcComputerName(m_utilitiesConfig.strComputerName);
    }
    else
    {
        SystemDetails::SetOrcComputerName(m_utilitiesConfig.strComputerName);
    }

    m_Config.locs.Consolidate(false, FSVBR::FSType::FAT);

    if (m_Config.output.Type == OutputSpec::Kind::None)
    {
        m_Config.output.Type = OutputSpec::Kind::TableFile;
        m_Config.output.Path = L"FatInfo.csv";
        m_Config.output.OutputEncoding = OutputSpec::Encoding::UTF8;
    }

    if (m_Config.output.Type == OutputSpec::Kind::Directory || m_Config.output.Type == OutputSpec::Kind::Archive)
    {
        m_Config.volumesStatsOutput.Path = m_Config.output.Path;
        m_Config.volumesStatsOutput.Type = m_Config.output.Type;
        m_Config.volumesStatsOutput.OutputEncoding = m_Config.output.OutputEncoding;
    }

    if (m_Config.DefaultIntentions == Intentions::FILEINFO_NONE)
    {
        m_Config.DefaultIntentions =
            FatFileInfo::GetIntentions(L"Default", FatFileInfo::g_FatAliasNames, FatFileInfo::g_FatColumnNames);
    }

    if (boost::logic::indeterminate(m_Config.bResurrectRecords))
    {
        m_Config.bResurrectRecords = true;
    }

    m_Config.ColumnIntentions = static_cast<Intentions>(
        m_Config.DefaultIntentions
        | (m_Config.Filters.empty() ? Intentions::FILEINFO_NONE : FileInfo::GetFilterIntentions(m_Config.Filters)));

    if (m_Config.output.Type == OutputSpec::Kind::Directory)
    {
        hr = ::VerifyDirectoryExists(m_Config.output.Path.c_str());
        if (FAILED(hr))
        {
            Log::Error(
                L"Specified file information output directory '{}' is not a directory (code: {:#x})",
                m_Config.output.Path,
                hr);
            return hr;
        }
    }

    return S_OK;
}
