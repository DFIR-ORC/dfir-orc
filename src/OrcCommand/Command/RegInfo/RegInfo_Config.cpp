//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//

#include "stdafx.h"

#include "SystemDetails.h"
#include "ParameterCheck.h"
#include "WideAnsi.h"
#include "TableOutputWriter.h"

#include "Configuration/ConfigFile.h"
#include "ConfigFile_RegInfo.h"

#include "Configuration/ConfigFileReader.h"

#include "FileFind.h"

#include "RegInfo.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command::RegInfo;

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return Orc::Config::RegInfo::root;
}

Main::RegInfoType Main::GetRegInfoType(LPCWSTR Params)
{
    if (Params == NULL)
        return REGINFO_NONE;

    RegInfoType retval = REGINFO_NONE;
    WCHAR szParams[MAX_PATH];
    WCHAR* pCur = szParams;

    StringCchCopy(szParams, MAX_PATH, Params);
    StringCchCat(szParams, MAX_PATH, L",");
    DWORD dwParamLen = (DWORD)wcslen(szParams);

    for (DWORD i = 0; i < dwParamLen; i++)
    {
        if (szParams[i] == L',' || szParams[i] == L'\0')
        {
            szParams[i] = 0;

            const RegInfoDescription* pCurType = _InfoDescription;
            while (pCurType->Type != REGINFO_NONE)
            {
                if (!_wcsicmp(pCur, pCurType->ColumnName))
                {
                    retval = static_cast<RegInfoType>(retval | pCurType->Type);
                    break;
                }
                pCurType++;
            }

            const RegInfoDescription* pCurAlias = _AliasDescription;
            while (pCurAlias->Type != REGINFO_NONE)
            {
                if (!_wcsicmp(pCur, pCurAlias->ColumnName))
                {
                    retval = static_cast<RegInfoType>(retval | pCurAlias->Type);
                    break;
                }
                pCurAlias++;
            }

            szParams[i] = L',';
            pCur = &(szParams[i]) + 1;
        }
    }
    return retval;
}

HRESULT Main::GetConfigurationFromConfig(const ConfigItem& configitem)
{
    HRESULT hr = E_FAIL;

    ConfigFile reader;

    if (FAILED(
            hr = config.Output.Configure(
                static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::Directory),
                configitem[REGINFO_OUTPUT])))
    {
        return hr;
    }
    else
    {
        Log::Debug("INFO: No statisfactory ouput in config file");
    }

    if (configitem[REGINFO_CSVLIMIT])
    {
        GetIntegerFromArg(configitem[REGINFO_CSVLIMIT].c_str(), config.CsvValueLengthLimit);
    }
    else
    {
        config.CsvValueLengthLimit = REGINFO_CSV_DEFALUT_LIMIT;
    }

    if (configitem[REGINFO_COMPUTER])
        Log::Info(L"No computer name specified ({})", configitem[REGINFO_INFORMATION].c_str());

    m_utilitiesConfig.strComputerName = configitem[REGINFO_COMPUTER];

    std::for_each(
        begin(configitem[REGINFO_HIVE].NodeList),
        end(configitem[REGINFO_HIVE].NodeList),
        [this, &reader](const ConfigItem& item) -> HRESULT {
            HRESULT hr = E_FAIL;

            auto NewQuery = std::make_shared<HiveQuery::SearchQuery>();
            std::vector<std::shared_ptr<FileFind::SearchTerm>> FileFindTerms;
            std::vector<std::wstring> FileNameList;

            hr = config.regFindConfig.GetConfiguration(
                item,
                config.m_HiveQuery.m_HivesLocation,
                config.m_HiveQuery.m_HivesFind,
                NewQuery->QuerySpec,
                FileNameList,
                FileFindTerms);
            if (FAILED(hr))
            {
                return hr;
            }

            config.m_HiveQuery.m_Queries.push_back(NewQuery);

            // Create link between FileFind query and RegfindResearch
            std::for_each(
                FileFindTerms.begin(), FileFindTerms.end(), [this, NewQuery](shared_ptr<FileFind::SearchTerm> term) {
                    config.m_HiveQuery.m_FileFindMap.insert(std::make_pair(term, NewQuery));
                });

            // Create link between regular hive files and RegfindResearch
            std::for_each(FileNameList.begin(), FileNameList.end(), [this, NewQuery](std::wstring name) {
                config.m_HiveQuery.m_HivesFileList.push_back(name);
                config.m_HiveQuery.m_FileNameMap.insert(std::make_pair(name, NewQuery));
            });

            return hr;
        });

    if (configitem[REGINFO_LOGGING])
    {
        Log::Warn(L"The '<logging> configuration element is deprecated, please use '<log>' instead");
    }

    if (configitem[REGINFO_LOG])
    {
        UtilitiesLoggerConfiguration::Parse(configitem[REGINFO_LOG], m_utilitiesConfig.log);
    }

    if (configitem[REGINFO_INFORMATION])
    {
        config.Information = GetRegInfoType(configitem[REGINFO_INFORMATION].c_str());
        if (config.Information == REGINFO_NONE)
        {
            Log::Error(L"Column specified '{}' is invalid", configitem[REGINFO_INFORMATION].c_str());
            return E_INVALIDARG;
        }
    }

    if (FAILED(hr = config.m_HiveQuery.m_HivesLocation.AddLocationsFromConfigItem(configitem[REGINFO_LOCATION])))
    {
        Log::Error("Error in specific locations parsing in config file [{}]", SystemError(hr));
        return hr;
    }

    if (FAILED(hr = config.m_HiveQuery.m_HivesLocation.AddKnownLocations(configitem[REGINFO_KNOWNLOCATIONS])))
    {
        Log::Error(L"Error in specific known locations parsing in config file [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
    UtilitiesLoggerConfiguration::Parse(argc, argv, m_utilitiesConfig.log);

    HRESULT hr = E_FAIL;
    try
    {

        for (int i = 1; i < argc; i++)
        {
            switch (argv[i][0])
            {
                case L'/':
                case L'-':
                    if (OutputOption(
                            argv[i] + 1,
                            L"Out",
                            static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::Directory),
                            config.Output))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Computer", m_utilitiesConfig.strComputerName))
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
                        config.Information = static_cast<RegInfoType>(config.Information | GetRegInfoType(argv[i] + 1));
                    }
                    break;
                default:
                    std::for_each(
                        config.m_HiveQuery.m_Queries.begin(),
                        config.m_HiveQuery.m_Queries.end(),
                        [this, argv, i](shared_ptr<HiveQuery::SearchQuery> Query) {
                            config.m_HiveQuery.m_HivesFileList.push_back(wstring(argv[i]));
                        }

                    );

                    break;
            }
        }
    }
    catch (...)
    {
        Log::Info("RegInfo failed during argument parsing, exiting");
        return E_FAIL;
    }
    return S_OK;
}

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaitem)
{
    config.Output.Schema = TableOutput::GetColumnsFromConfig(
        config.Output.TableKey.empty() ? L"reginfo" : config.Output.TableKey.c_str(), schemaitem);
    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    UtilitiesLoggerConfiguration::Apply(m_logging, m_utilitiesConfig.log);

    config.m_HiveQuery.m_HivesLocation.Consolidate(false, FSVBR::FSType::NTFS);

    if (config.Output.Type == OutputSpec::Kind::None)
    {
        Log::Error("No valid output specified (only directory or csv|tsv are allowed");
        return E_INVALIDARG;
    }

    if (HasFlag(config.Output.Type, OutputSpec::Kind::Archive))
    {
        Log::Error("Archive output is not supported (only directory or csv|tsv are allowed");
        return E_INVALIDARG;
    }

    return S_OK;
}
