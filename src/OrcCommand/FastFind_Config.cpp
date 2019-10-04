//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <string>

#include "FastFind.h"

#include "LogFileWriter.h"
#include "SystemDetails.h"
#include "ParameterCheck.h"
#include "WideAnsi.h"
#include "TableOutputWriter.h"

#include "ConfigFile.h"
#include "ConfigFile_FastFind.h"

#include "RegFindConfig.h"

#include "YaraScanner.h"

#include "ConfigFileReader.h"

#include "FileFind.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command::FastFind;

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaitem)
{
    config.outFileSystem.Schema = TableOutput::GetColumnsFromConfig(
        _L_,
        config.outFileSystem.TableKey.empty() ? L"FastFindFileSystem" : config.outFileSystem.TableKey.c_str(),
        schemaitem);
    config.outRegsitry.Schema = TableOutput::GetColumnsFromConfig(
        _L_,
        config.outRegsitry.TableKey.empty() ? L"FastFindRegistry" : config.outRegsitry.TableKey.c_str(),
        schemaitem);
    config.outObject.Schema = TableOutput::GetColumnsFromConfig(
        _L_,
        config.outObject.TableKey.empty() ? L"FastFindObject" : config.outObject.TableKey.c_str(),
        schemaitem);
    return S_OK;
}

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return Orc::Config::FastFind::root;
}

HRESULT Main::GetConfigurationFromConfig(const ConfigItem& configitem)
{
    HRESULT hr = E_FAIL;

    ConfigFile::ConfigureLogging(configitem[FASTFIND_LOGGING], _L_);

    if (configitem[FASTFIND_VERSION])
    {
        config.strVersion = configitem[FASTFIND_VERSION];
    }

    ConfigFile reader(_L_);

    if (FAILED(
            hr = config.outFileSystem.Configure(
                _L_,
                static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile),
                configitem[FASTFIND_OUTPUT_FILESYSTEM])))
    {
        return hr;
    }
    if (FAILED(
            hr = config.outRegsitry.Configure(
                _L_, static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile), configitem[FASTFIND_OUTPUT_REGISTRY])))
    {
        return hr;
    }
    if (FAILED(
            hr = config.outObject.Configure(
                _L_, static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile), configitem[FASTFIND_OUTPUT_OBJECT])))
    {
        return hr;
    }
    if (FAILED(
            hr = config.outStructured.Configure(
                _L_,
                static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile),
                configitem[FASTFIND_OUTPUT_STRUCTURED])))
    {
        return hr;
    }

    if (configitem[FASTFIND_FILESYSTEM])
    {
        const ConfigItem& filesystem = configitem[FASTFIND_FILESYSTEM];
        if (FAILED(
                hr = config.FileSystem.Locations.AddLocationsFromConfigItem(filesystem[FASTFIND_FILESYSTEM_LOCATIONS])))
        {
            log::Error(_L_, hr, L"Error in specific locations parsing in config file\r\n");
            return hr;
        }

        if (FAILED(hr = config.FileSystem.Locations.AddKnownLocations(filesystem[FASTFIND_FILESYSTEM_KNOWNLOCATIONS])))
        {
            log::Error(_L_, hr, L"Error in knownlocations parsing in config file\r\n");
            return hr;
        }

        if (FAILED(hr = config.FileSystem.Files.AddTermsFromConfig(filesystem[FASTFIND_FILESYSTEM_FILEFIND])))
        {
            log::Error(_L_, hr, L"Error in specific file find parsing in config file\r\n");
            return hr;
        }
        if (FAILED(hr = config.FileSystem.Files.AddExcludeTermsFromConfig(filesystem[FASTFIND_FILESYSTEM_EXCLUDE])))
        {
            log::Error(_L_, hr, L"Error in specific file find parsing in config file\r\n");
            return hr;
        }
        if (filesystem[FASTFIND_FILESYSTEM_YARA])
        {
            config.Yara = std::make_unique<YaraConfig>(YaraConfig::Get(_L_, filesystem[FASTFIND_FILESYSTEM_YARA]));
        }
    }

    if (configitem[FASTFIND_REGISTRY])
    {
        if (FAILED(
                hr = config.Registry.Locations.AddLocationsFromConfigItem(
                    configitem[FASTFIND_REGISTRY][FASTFIND_REGISTRY_LOCATIONS])))
        {
            log::Error(_L_, hr, L"Error in specific locations parsing in config file\r\n");
            return hr;
        }

        if (FAILED(
                hr = config.Registry.Locations.AddKnownLocations(
                    configitem[FASTFIND_REGISTRY][FASTFIND_REGISTRY_KNOWNLOCATIONS])))
        {
            log::Error(_L_, hr, L"Error in knownlocations parsing in config file\r\n");
            return hr;
        }

        std::for_each(
            begin(configitem[FASTFIND_REGISTRY][FASTFIND_REGISTRY_HIVE].NodeList),
            end(configitem[FASTFIND_REGISTRY][FASTFIND_REGISTRY_HIVE].NodeList),
            [this, &hr](const ConfigItem& item) {
                RegFindConfig regconfig(_L_);

                std::vector<std::shared_ptr<FileFind::SearchTerm>> FileFindTerms;
                std::vector<std::wstring> FileNameList;

                RegFind regfind(_L_);

                if (FAILED(
                        hr = regconfig.GetConfiguration(
                            item,
                            config.Registry.Locations,
                            config.Registry.Files,
                            regfind,
                            FileNameList,
                            FileFindTerms)))
                {
                    log::Error(_L_, hr, L"Error in specific registry find parsing un config file\r\n");
                    return hr;
                }

                config.Registry.RegistryFind.push_back(std::move(regfind));
                return hr;
            });
    }

    if (configitem[FASTFIND_OBJECT])
    {
        for (const auto& item : configitem[FASTFIND_OBJECT][FASTFIND_OBJECT_FIND].NodeList)
        {
            ObjectSpec::ObjectItem anItem;

            if (item[FASTFIND_OBJECT_FIND_TYPE])
            {
                anItem.ObjType = ObjectDirectory::GetObjectType(item[FASTFIND_OBJECT_FIND_TYPE]);
                if (anItem.ObjType == ObjectDirectory::ObjectType::Invalid)
                {
                    log::Warning(
                        _L_,
                        E_INVALIDARG,
                        L"Invalid object type provided: %s\r\n",
                        item[FASTFIND_OBJECT_FIND_TYPE].strData.c_str());
                    continue;
                }

                if (item[FASTFIND_OBJECT_FIND_NAME])
                {
                    anItem.strName = item[FASTFIND_OBJECT_FIND_NAME];
                    anItem.name_typeofMatch = ObjectSpec::MatchType::Exact;
                }
                else if (item[FASTFIND_OBJECT_FIND_NAME_MATCH])
                {
                    anItem.strName = item[FASTFIND_OBJECT_FIND_NAME_MATCH];
                    anItem.name_typeofMatch = ObjectSpec::MatchType::Match;
                }
                else if (item[FASTFIND_OBJECT_FIND_NAME_REGEX])
                {
                    anItem.strName = item[FASTFIND_OBJECT_FIND_NAME_REGEX];
                    anItem.name_regexp = std::make_unique<std::wregex>(anItem.strName);
                    anItem.name_typeofMatch = ObjectSpec::MatchType::Regex;
                }

                if (item[FASTFIND_OBJECT_FIND_PATH])
                {
                    anItem.strPath = item[FASTFIND_OBJECT_FIND_PATH];
                    anItem.path_typeofMatch = ObjectSpec::MatchType::Exact;
                }
                else if (item[FASTFIND_OBJECT_FIND_PATH_MATCH])
                {
                    anItem.strPath = item[FASTFIND_OBJECT_FIND_PATH_MATCH];
                    anItem.path_typeofMatch = ObjectSpec::MatchType::Match;
                }
                else if (item[FASTFIND_OBJECT_FIND_PATH_REGEX])
                {
                    anItem.strPath = item[FASTFIND_OBJECT_FIND_PATH_REGEX];
                    anItem.path_regexp = std::make_unique<std::wregex>(anItem.strPath);
                    anItem.path_typeofMatch = ObjectSpec::MatchType::Regex;
                }
                config.Object.Items.push_back(anItem);
            }
        }
    }
    return S_OK;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
    HRESULT hr = E_FAIL;

    for (int i = 0; i < argc; i++)
    {
        switch (argv[i][0])
        {
            case L'/':
            case L'-':
                if (ProcessPriorityOption(argv[i]))
                    ;
                else if (BooleanOption(argv[i] + 1, L"SkipDeleted", config.bSkipDeleted))
                    ;
                else if (BooleanOption(argv[i] + 1, L"Shadows", config.bAddShadows))
                    ;
                else if (!_wcsnicmp(argv[i] + 1, L"Names", wcslen(L"Names")))
                {
                    LPCWSTR pEquals = wcschr(argv[i], L'=');
                    if (!pEquals)
                    {
                        log::Error(
                            _L_,
                            E_INVALIDARG,
                            L"Option /Names should be like: /Names=Kernel32.dll,nt*.sys,:ADSName,*.txt#EAName\r\n");
                        return E_INVALIDARG;
                    }
                    else
                    {
                        std::wstring strNames(pEquals + 1);
                        std::wregex re(L",");

                        std::wsregex_token_iterator iter(begin(strNames), end(strNames), re, -1);
                        std::wsregex_token_iterator last;

                        std::for_each(iter, last, [this](const wstring& item) {
                            shared_ptr<FileFind::SearchTerm> fs = make_shared<FileFind::SearchTerm>();
                            fs->Name = item;
                            fs->Required = FileFind::SearchTerm::NAME;
                            config.FileSystem.Files.AddTerm(fs);
                        });
                    }
                }
                else if (ParameterOption(argv[i] + 1, L"Version", config.strVersion))
                    ;
                else if (OutputOption(
                             argv[i] + 1,
                             L"filesystem",
                             static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile),
                             config.outFileSystem))
                    ;
                else if (OutputOption(
                             argv[i] + 1,
                             L"registry",
                             static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile),
                             config.outRegsitry))
                    ;
                else if (OutputOption(
                             argv[i] + 1,
                             L"object",
                             static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile),
                             config.outObject))
                    ;
                else if (OutputOption(
                             argv[i] + 1,
                             L"out",
                             static_cast<OutputSpec::Kind>(
                                 OutputSpec::Kind::Directory | OutputSpec::Kind::StructuredFile),
                             config.outStructured))
                    ;
                else if (ParameterOption(argv[i] + 1, L"Yara", config.YaraSource))
                {
                    if (!config.Yara)
                        config.Yara = std::make_unique<YaraConfig>();
                    boost::split(config.Yara->Sources(), config.YaraSource, boost::is_any_of(";,"));
                }
                else if (AltitudeOption(argv[i] + 1, L"Altitude", config.FileSystem.Locations.GetAltitude()))
                {
                    config.Registry.Locations.GetAltitude() = config.FileSystem.Locations.GetAltitude();
                }
                else if (IgnoreCommonOptions(argv[i] + 1))
                    ;
                else if (UsageOption(argv[i] + 1))
                {
                }
                break;
            default:
                break;
        }
    }

    return config.FileSystem.Locations.AddLocationsFromArgcArgv(argc, argv);
}

HRESULT Main::CheckConfiguration()
{
    HRESULT hr = E_FAIL;

    bool bSomeThingToParse = false;

    config.FileSystem.Locations.Consolidate(config.bAddShadows, FSVBR::FSType::NTFS);

    for (const auto& loc : config.FileSystem.Locations.GetAltitudeLocations())
    {
        bSomeThingToParse |= loc->GetParse();
    }

    if (FAILED(hr = config.FileSystem.Files.InitializeYara(config.Yara)))
    {
        log::Error(_L_, hr, L"Failed to initialize yara scanner\r\n");
        return hr;
    }

    if (FAILED(hr = config.FileSystem.Files.CheckYara()))
        return hr;

    if (!bSomeThingToParse)
    {
        for (const auto& loc : config.FileSystem.Locations.GetAltitudeLocations())
        {
            loc->SetParse(true);
        }
    }

    config.Registry.Locations.Consolidate(config.bAddShadows, FSVBR::FSType::NTFS);

    if (ObjectDirs.empty())
    {
        ObjectDirs.emplace_back(L"\\");
    }
    if (FileDirs.empty())
    {
        FileDirs.emplace_back(L"\\\\.\\pipe\\");
    }

    return S_OK;
}
