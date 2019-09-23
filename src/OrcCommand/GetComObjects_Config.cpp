//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "GetComObjects.h"
#include "LogFileWriter.h"
#include "SystemDetails.h"
#include "TableOutputWriter.h"

#include "ConfigFile_GetComObjects.h"

using namespace Orc;
using namespace Orc::Command::GetComObjects;

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    // This tool does not have an xml config file
    return Orc::Config::GetComObjects::root;
}

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaitem)
{
    m_Config.m_Output.Schema = TableOutput::GetColumnsFromConfig(
        _L_, m_Config.m_Output.TableKey.empty() ? L"comobjects" : m_Config.m_Output.TableKey.c_str(), schemaitem);
    return S_OK;
}

HRESULT Main::GetConfigurationFromConfig(const ConfigItem& configitem)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Config.m_Output.Configure(_L_, configitem[GETCOMOBJECTS_OUTPUT])))
    {
        log::Error(_L_, hr, L"Invalid output specified\r\n");
        return hr;
    }

    if (configitem[GETCOMOBJECTS_COMPUTER])
        m_Config.m_StrComputerName = configitem[GETCOMOBJECTS_COMPUTER];

    std::for_each(
        std::begin(configitem[GETCOMOBJECTS_HIVE].NodeList),
        std::end(configitem[GETCOMOBJECTS_HIVE].NodeList),
        [this](const ConfigItem& item) -> HRESULT {
            HRESULT hr = E_FAIL;

            auto NewQuery = std::make_shared<HiveQuery::SearchQuery>(_L_);
            std::vector<std::shared_ptr<FileFind::SearchTerm>> FileFindTerms;
            std::vector<std::wstring> FileNameList;

            if (FAILED(
                    hr = m_Config.m_RegFindConfig.GetConfiguration(
                        item,
                        m_Config.m_HiveQuery.m_HivesLocation,
                        m_Config.m_HiveQuery.m_HivesFind,
                        NewQuery->QuerySpec,
                        FileNameList,
                        FileFindTerms)))
            {
                return hr;
            }

            m_Config.m_HiveQuery.m_Queries.push_back(NewQuery);

            // Create link between FileFind query and RegfindResearch
            std::for_each(
                FileFindTerms.begin(),
                FileFindTerms.end(),
                [this, NewQuery](std::shared_ptr<FileFind::SearchTerm> term) {
                    m_Config.m_HiveQuery.m_FileFindMap.insert(std::make_pair(term, NewQuery));
                });

            // Create link between regular hive files and RegfindResearch
            std::for_each(FileNameList.begin(), FileNameList.end(), [this, NewQuery](std::wstring name) {
                m_Config.m_HiveQuery.m_HivesFileList.push_back(name);
                m_Config.m_HiveQuery.m_FileNameMap.insert(std::make_pair(name, NewQuery));
            });

            return hr;
        });

    if (FAILED(
            hr = m_Config.m_HiveQuery.m_HivesLocation.AddLocationsFromConfigItem(configitem[GETCOMOBJECTS_LOCATIONS])))
    {
        log::Error(_L_, hr, L"Error in specific locations parsing in config file\r\n");
        return hr;
    }

    return S_OK;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
    // Configuration is completed with command line args

    bool bBool = false;
    for (int i = 1; i < argc; i++)
    {
        switch (argv[i][0])
        {
            case L'/':
            case L'-':
                if (OutputOption(argv[i] + 1, L"out", m_Config.m_Output))
                    ;
                else if (ProcessPriorityOption(argv[i] + 1))
                    ;
                else if (ParameterOption(argv[i] + 1, L"Computer", m_Config.m_StrComputerName))
                    ;
                else if (UsageOption(argv[i] + 1))
                    ;
                else if (IgnoreCommonOptions(argv[i] + 1))
                    ;
                else
                {
                    PrintUsage();
                    return E_INVALIDARG;
                }
                break;
            default:
                std::for_each(
                    m_Config.m_HiveQuery.m_Queries.begin(),
                    m_Config.m_HiveQuery.m_Queries.end(),
                    [this, argv, i](std::shared_ptr<HiveQuery::SearchQuery> Query) {
                        m_Config.m_HiveQuery.m_HivesFileList.push_back(std::wstring(argv[i]));
                    });

                break;
        }
    }

    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Config.m_HiveQuery.m_HivesLocation.AddLocationsFromArgcArgv(argc, argv)))
    {
        log::Error(_L_, hr, L"Error in specific locations parsing in config file\r\n");
        return hr;
    }

    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    HRESULT hr = E_FAIL;

    if (m_Config.m_StrComputerName.empty())
    {
        SystemDetails::GetOrcComputerName(m_Config.m_StrComputerName);
    }

    if (m_Config.m_Output.Type == OutputSpec::Kind::None)
    {
        m_Config.m_Output.Type = OutputSpec::Kind::TableFile;
        m_Config.m_Output.Path = L"ComObjects.csv";
        m_Config.m_Output.OutputEncoding = OutputSpec::Encoding::UTF8;
    }

    m_Config.m_HiveQuery.m_HivesLocation.Consolidate(false, FSVBR::FSType::ALL);

    if (m_Config.m_Output.Type == OutputSpec::Kind::Directory)
    {
        if (FAILED(hr = ::VerifyDirectoryExists(m_Config.m_Output.Path.c_str())))
        {
            log::Error(
                _L_,
                hr,
                L"Specified file information output directory (%s) is not a directory\r\n",
                m_Config.m_Output.Path.c_str());
            return hr;
        }
    }

    return S_OK;
}
