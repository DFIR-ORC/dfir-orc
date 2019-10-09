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
#include "LogFileWriter.h"

using namespace Orc;
using namespace Orc::Command::FatInfo;

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return Orc::Config::FatInfo::root;
}

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaitem)
{
    m_Config.output.Schema = TableOutput::GetColumnsFromConfig(
        _L_, m_Config.output.TableKey.empty() ? L"fileinfo" : m_Config.output.TableKey.c_str(), schemaitem);
    m_Config.volumesStatsOutput.Schema = TableOutput::GetColumnsFromConfig(
        _L_,
        m_Config.volumesStatsOutput.TableKey.empty() ? L"volstats" : m_Config.volumesStatsOutput.TableKey.c_str(),
        schemaitem);
    return S_OK;
}

HRESULT Main::GetColumnsAndFiltersFromConfig(const ConfigItem& configItem)
{
    HRESULT hr = E_FAIL;

    const ConfigItem& configitem = configItem[FATINFO_COLUMNS];

    if (configitem[WRITERRORS])
    {
        using namespace std::string_view_literals;
        constexpr auto YES = L"yes"sv;

        if (equalCaseInsensitive((const std::wstring&)configitem.SubItems[WRITERRORS], YES))
            m_Config.bWriteErrorCodes = true;
        else
            m_Config.bWriteErrorCodes = false;
    }

    std::for_each(
        begin(configitem[DEFAULT].NodeList), end(configitem[DEFAULT].NodeList), [this](const ConfigItem& item) {
            if (item)
                m_Config.DefaultIntentions = static_cast<Intentions>(
                    m_Config.DefaultIntentions
                    | FileInfo::GetIntentions(
                        _L_, item.strData.c_str(), FatFileInfo::g_FatAliasNames, FatFileInfo::g_FatColumnNames));
        });

    std::for_each(begin(configitem[ADD].NodeList), end(configitem[ADD].NodeList), [this](const ConfigItem& item) {
        HRESULT hr1 = E_FAIL;
        Filter filter;
        filter.bInclude = true;

        if (FAILED(
                hr1 = FileInfoCommon::GetFilterFromConfig(
                    _L_, item, filter, FatFileInfo::g_FatAliasNames, FatFileInfo::g_FatColumnNames)))
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
            _L_, item, filter, FatFileInfo::g_FatAliasNames, FatFileInfo::g_FatColumnNames);

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

    if (FAILED(hr = m_Config.output.Configure(_L_, configitem[FATINFO_OUTPUT])))
    {
        log::Error(_L_, hr, L"Invalid output specified\r\n");
        return hr;
    }

    if (configitem[FATINFO_COMPUTER])
        m_Config.strComputerName = configitem[FATINFO_COMPUTER];

    m_Config.bResurrectRecords = GetResurrectFromConfig(configitem);
    m_Config.bPopSystemObjects = GetPopulateSystemObjectsFromConfig(configitem);

    if (boost::logic::indeterminate(m_Config.bPopSystemObjects))
        m_Config.bPopSystemObjects = false;
    m_Config.locs.SetPopulateSystemObjects((bool)m_Config.bPopSystemObjects);

    if (FAILED(hr = m_Config.locs.AddLocationsFromConfigItem(configitem[FATINFO_LOCATIONS])))
    {
        log::Error(_L_, hr, L"Failed to get locations definition from config\r\n");
        return hr;
    }

    if (FAILED(hr = GetColumnsAndFiltersFromConfig(configitem)))
    {
        log::Error(_L_, hr, L"Failed to get column definition from config\r\n");
        return hr;
    }

    return S_OK;
}

boost::logic::tribool Main::GetResurrectFromConfig(const ConfigItem& config)
{
    if (config[FATINFO_RESURRECT].Status & ConfigItem::PRESENT)
    {
        if (!_wcsnicmp(config[FATINFO_RESURRECT].strData.c_str(), L"no", wcslen(L"no")))
            return false;
        else
            return true;
    }
    return boost::logic::indeterminate;
}

boost::logic::tribool Main::GetPopulateSystemObjectsFromConfig(const ConfigItem& config)
{
    if (config[FATINFO_POP_SYS_OBJ].Status & ConfigItem::PRESENT)
    {
        if (!_wcsnicmp(config[FATINFO_POP_SYS_OBJ].strData.c_str(), L"no", wcslen(L"no")))
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
                else if (ParameterOption(argv[i] + 1, L"Computer", m_Config.strComputerName))
                    ;
                else if (BooleanOption(argv[i] + 1, L"errorcodes", m_Config.bWriteErrorCodes))
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
                                    _L_, pCur, FatFileInfo::g_FatAliasNames, FatFileInfo::g_FatColumnNames));
                            pCur = pNext + 1;
                        }
                        else
                        {
                            m_Config.DefaultIntentions = static_cast<Intentions>(
                                m_Config.DefaultIntentions
                                | FatFileInfo::GetIntentions(
                                    _L_, pCur, FatFileInfo::g_FatAliasNames, FatFileInfo::g_FatColumnNames));
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
                _L_, argc, argv, m_Config.Filters, FatFileInfo::g_FatAliasNames, FatFileInfo::g_FatColumnNames)))
        return hr;

    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    HRESULT hr = E_FAIL;

    if (m_Config.strComputerName.empty())
    {
        SystemDetails::GetOrcComputerName(m_Config.strComputerName);
    }
	else
	{
		SystemDetails::SetOrcComputerName(m_Config.strComputerName);
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

    if (m_Config.DefaultIntentions == FILEINFO_NONE)
    {
        m_Config.DefaultIntentions =
            FatFileInfo::GetIntentions(_L_, L"Default", FatFileInfo::g_FatAliasNames, FatFileInfo::g_FatColumnNames);
    }

    if (boost::logic::indeterminate(m_Config.bResurrectRecords))
    {
        m_Config.bResurrectRecords = true;
    }

    m_Config.ColumnIntentions = static_cast<Intentions>(
        m_Config.DefaultIntentions
        | (m_Config.Filters.empty() ? FILEINFO_NONE : FileInfo::GetFilterIntentions(m_Config.Filters)));

    if (m_Config.output.Type == OutputSpec::Kind::Directory)
    {
        if (FAILED(hr = ::VerifyDirectoryExists(m_Config.output.Path.c_str())))
        {
            log::Error(
                _L_,
                hr,
                L"Specified file information output directory (%s) is not a directory\r\n",
                m_Config.output.Path.c_str());
            return hr;
        }
    }

    return S_OK;
}
