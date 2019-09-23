//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//

#include "stdafx.h"

#include "LogFileWriter.h"
#include "SystemDetails.h"
#include "ParameterCheck.h"
#include "WideAnsi.h"
#include "TableOutputWriter.h"

#include "ConfigFile.h"
#include "ConfigFile_RegInfo.h"

#include "ConfigFileReader.h"

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

    ConfigFile reader(_L_);

    reader.ConfigureLogging(configitem[REGINFO_LOGGING], _L_);

    if (FAILED(
            hr = config.Output.Configure(
                _L_,
                static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::Directory),
                configitem[REGINFO_OUTPUT])))
    {
        return hr;
    }
    else
    {
        log::Verbose(_L_, L"INFO: No statifactory ouput in config file\r\n");
    }

    if (configitem[REGINFO_CSVLIMIT])
    {
        GetIntegerFromArg(configitem[REGINFO_CSVLIMIT].strData.c_str(), config.CsvValueLengthLimit);
    }
    else
    {
        config.CsvValueLengthLimit = REGINFO_CSV_DEFALUT_LIMIT;
    }

    if (configitem[REGINFO_COMPUTER])
        log::Info(_L_, L"No computer name specified\r\n", configitem[REGINFO_INFORMATION].strData.c_str());

    config.strComputerName = configitem[REGINFO_COMPUTER].strData;

    std::for_each(
        begin(configitem[REGINFO_HIVE].NodeList),
        end(configitem[REGINFO_HIVE].NodeList),
        [this, &reader](const ConfigItem& item) -> HRESULT {
            HRESULT hr = E_FAIL;

            auto NewQuery = std::make_shared<HiveQuery::SearchQuery>(_L_);
            std::vector<std::shared_ptr<FileFind::SearchTerm>> FileFindTerms;
            std::vector<std::wstring> FileNameList;

            if (FAILED(
                    hr = config.regFindConfig.GetConfiguration(
                        item,
                        config.m_HiveQuery.m_HivesLocation,
                        config.m_HiveQuery.m_HivesFind,
                        NewQuery->QuerySpec,
                        FileNameList,
                        FileFindTerms)))
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

    if (configitem[REGINFO_INFORMATION])
    {
        config.Information = GetRegInfoType(configitem[REGINFO_INFORMATION].strData.c_str());
        if (config.Information == REGINFO_NONE)
        {
            log::Error(
                _L_,
                E_INVALIDARG,
                L"Column specified (%s) is invalid\r\n",
                configitem[REGINFO_INFORMATION].strData.c_str());
            return E_INVALIDARG;
        }
    }

    if (FAILED(hr = config.m_HiveQuery.m_HivesLocation.AddLocationsFromConfigItem(configitem[REGINFO_LOCATION])))
    {
        log::Error(_L_, hr, L"Error in specific locations parsing in config file\r\n");
        return hr;
    }

    if (FAILED(hr = config.m_HiveQuery.m_HivesLocation.AddKnownLocations(configitem[REGINFO_KNOWNLOCATIONS])))
    {
        log::Error(_L_, hr, L"Error in specific known locations parsing in config file\r\n");
        return hr;
    }

    return S_OK;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
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
                    else if (ParameterOption(argv[i] + 1, L"Computer", config.strComputerName))
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
        log::Info(_L_, L"RegInfo failed during argument parsing, exiting\r\n");
        return E_FAIL;
    }
    return S_OK;
}

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaitem)
{
    config.Output.Schema = TableOutput::GetColumnsFromConfig(
        _L_, config.Output.TableKey.empty() ? L"reginfo" : config.Output.TableKey.c_str(), schemaitem);
    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    config.m_HiveQuery.m_HivesLocation.Consolidate(false, FSVBR::FSType::ALL);

    return S_OK;
}
