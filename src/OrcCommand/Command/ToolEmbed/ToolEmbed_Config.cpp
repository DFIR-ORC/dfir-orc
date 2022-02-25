//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <string>

#include "ToolEmbed.h"
#include "ParameterCheck.h"
#include "EmbeddedResource.h"
#include "SystemDetails.h"

#include "ConfigFile_ToolEmbed.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command::ToolEmbed;

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return Orc::Config::ToolEmbed::root;
}

HRESULT
Main::GetNameValuePairFromConfigItem(const ConfigItem& item, EmbeddedResource::EmbedSpec& spec)
{
    if (wcscmp(item.strName.c_str(), L"pair"))
    {
        Log::Debug("item passed is not a name value pair");
        return E_FAIL;
    }

    if (!item[TOOLEMBED_PAIRNAME])
    {
        Log::Debug("name attribute is missing in name value pair");
        return E_FAIL;
    }

    if (!item[TOOLEMBED_PAIRVALUE])
    {
        Log::Debug("value attribute is missing in name value pair");
        return E_FAIL;
    }

    spec = EmbeddedResource::EmbedSpec::AddNameValuePair(item[TOOLEMBED_PAIRNAME], item[TOOLEMBED_PAIRVALUE]);
    return S_OK;
}

HRESULT Main::GetAddFileFromConfigItem(const ConfigItem& item, EmbeddedResource::EmbedSpec& spec)
{
    if (wcscmp(item.strName.c_str(), L"file"))
    {
        Log::Debug("item passed is not a file addition request");
        return E_FAIL;
    }

    if (!item[TOOLEMBED_FILENAME])
    {
        Log::Debug("name attribute is missing in file addition request");
        return E_FAIL;
    }

    if (!item[TOOLEMBED_FILEPATH])
    {
        Log::Debug("path attribute is missing in file addition request");
        return E_FAIL;
    }

    wstring strInputFile;

    if (item)
    {
        HRESULT hr = E_FAIL;
        if (FAILED(hr = ExpandFilePath(((const std::wstring&)item[TOOLEMBED_FILEPATH]).c_str(), strInputFile)))
        {
            Log::Error(
                L"Error in specified file '{}' to add in config file [{}]",
                item[TOOLEMBED_FILEPATH].c_str(),
                SystemError(hr));
            return hr;
        }
    }

    spec = EmbeddedResource::EmbedSpec::AddFile((const std::wstring&)item[TOOLEMBED_FILENAME], std::move(strInputFile));
    return S_OK;
}

HRESULT Main::GetAddArchiveFromConfigItem(const ConfigItem& item, EmbeddedResource::EmbedSpec& spec)
{
    HRESULT hr = E_FAIL;

    if (wcscmp(item.strName.c_str(), L"archive"))
    {
        Log::Debug("item passed is not a archive addition request");
        return E_FAIL;
    }

    if (!item[TOOLEMBED_ARCHIVE_NAME])
    {
        Log::Debug("name attribute is missing in archive addition request");
        return E_FAIL;
    }

    wstring strArchiveName = (const std::wstring&)item[TOOLEMBED_ARCHIVE_NAME];

    wstring strArchiveFormat;
    if (item[TOOLEMBED_ARCHIVE_FORMAT])
    {
        strArchiveFormat = (const std::wstring&)item[TOOLEMBED_ARCHIVE_FORMAT];
    }

    wstring strArchiveCompression;
    if (item[TOOLEMBED_ARCHIVE_COMPRESSION])
    {
        strArchiveCompression = (const std::wstring&)item[TOOLEMBED_ARCHIVE_COMPRESSION];
    }

    if (!item[TOOLEMBED_FILE2ARCHIVE])
    {
        Log::Debug("files to archive items are missing in archive addition request");
        return E_FAIL;
    }

    std::vector<EmbeddedResource::EmbedSpec::ArchiveItem> items;

    hr = S_OK;
    std::for_each(
        begin(item[TOOLEMBED_FILE2ARCHIVE].NodeList),
        end(item[TOOLEMBED_FILE2ARCHIVE].NodeList),
        [&items, &hr](const ConfigItem& item2cab) {
            EmbeddedResource::EmbedSpec::ArchiveItem toCab;

            toCab.Name = item2cab[TOOLEMBED_FILE2ARCHIVE_NAME];

            wstring strInputFile;

            HRESULT subhr = E_FAIL;
            if (FAILED(subhr = ExpandFilePath(item2cab[TOOLEMBED_FILE2ARCHIVE_PATH].c_str(), strInputFile)))
            {
                Log::Error(
                    L"Error in specified file '{}' to add to archive [{}]",
                    item2cab[TOOLEMBED_FILE2ARCHIVE_PATH].c_str(),
                    SystemError(subhr));
                hr = subhr;
                return;
            }

            std::swap(toCab.Path, strInputFile);

            items.push_back(toCab);
        });

    if (FAILED(hr))
        return hr;

    spec = EmbeddedResource::EmbedSpec::AddArchive(
        std::move(strArchiveName), std::move(strArchiveFormat), std::move(strArchiveCompression), std::move(items));

    return S_OK;
}

HRESULT Main::GetConfigurationFromConfig(const ConfigItem& configitem)
{
    HRESULT hr = E_FAIL;

    ConfigFile reader;

    {
        if (FAILED(hr = reader.GetInputFile(configitem[TOOLEMBED_INPUT], config.strInputFile)))
            return hr;
    }

    {
        if (FAILED(
                hr = config.Output.Configure(
                    static_cast<OutputSpec::Kind>(OutputSpec::Kind::File | OutputSpec::Kind::Directory),
                    configitem[TOOLEMBED_OUTPUT])))
        {
            return hr;
        }
    }

    hr = S_OK;

    if (configitem[TOOLEMBED_PAIR])
    {
        std::for_each(
            begin(configitem[TOOLEMBED_PAIR].NodeList),
            end(configitem[TOOLEMBED_PAIR].NodeList),
            [this, &hr](const ConfigItem& item) {
                HRESULT local_hr = E_FAIL;
                EmbeddedResource::EmbedSpec spec = EmbeddedResource::EmbedSpec::AddVoid();

                if (SUCCEEDED(local_hr = GetNameValuePairFromConfigItem(item, spec)))
                {
                    if (spec.Type != EmbeddedResource::EmbedSpec::EmbedType::Void)
                        config.ToEmbed.push_back(std::move(spec));
                }
                else
                    hr = local_hr;
            });
    }

    if (FAILED(hr))
        return hr;
    hr = S_OK;

    if (configitem[TOOLEMBED_FILE])
    {
        std::for_each(
            begin(configitem[TOOLEMBED_FILE].NodeList),
            end(configitem[TOOLEMBED_FILE].NodeList),
            [this, &hr](const ConfigItem& item) {
                HRESULT local_hr = E_FAIL;
                EmbeddedResource::EmbedSpec spec = EmbeddedResource::EmbedSpec::AddVoid();

                if (SUCCEEDED(local_hr = GetAddFileFromConfigItem(item, spec)))
                {
                    if (spec.Type != EmbeddedResource::EmbedSpec::EmbedType::Void)
                        config.ToEmbed.push_back(std::move(spec));
                }
                else
                    hr = local_hr;
            });
    }

    if (FAILED(hr))
        return hr;
    hr = S_OK;

    if (configitem[TOOLEMBED_ARCHIVE])
    {
        std::for_each(
            begin(configitem[TOOLEMBED_ARCHIVE].NodeList),
            end(configitem[TOOLEMBED_ARCHIVE].NodeList),
            [this, &hr](const ConfigItem& item) {
                HRESULT local_hr = E_FAIL;
                EmbeddedResource::EmbedSpec spec = EmbeddedResource::EmbedSpec::AddVoid();

                if (SUCCEEDED(local_hr = GetAddArchiveFromConfigItem(item, spec)))
                {
                    if (spec.Type != EmbeddedResource::EmbedSpec::EmbedType::Void)
                        config.ToEmbed.push_back(std::move(spec));
                }
                else
                    hr = local_hr;
            });
    }

    if (FAILED(hr))
        return hr;

    if (configitem[TOOLEMBED_LOGGING])
    {
        Log::Warn(L"The '<logging> configuration element is deprecated, please use '<log>' instead");
    }

    if (configitem[TOOLEMBED_LOG])
    {
        UtilitiesLoggerConfiguration::Parse(configitem[TOOLEMBED_LOG], m_utilitiesConfig.log);
    }

    if (configitem[TOOLEMBED_RUN])
    {
        config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddRun(configitem[TOOLEMBED_RUN]));
        if (configitem[TOOLEMBED_RUN][TOOLEMBED_RUN_ARGS])
            config.ToEmbed.push_back(
                EmbeddedResource::EmbedSpec::AddRunArgs(configitem[TOOLEMBED_RUN][TOOLEMBED_RUN_ARGS]));
    }
    if (configitem[TOOLEMBED_RUN32])
    {
        config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddRunX86(configitem[TOOLEMBED_RUN32]));
        if (configitem[TOOLEMBED_RUN32][TOOLEMBED_RUN_ARGS])
            config.ToEmbed.push_back(
                EmbeddedResource::EmbedSpec::AddRun32Args(configitem[TOOLEMBED_RUN32][TOOLEMBED_RUN_ARGS]));
    }
    if (configitem[TOOLEMBED_RUN64])
    {
        config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddRunX64(configitem[TOOLEMBED_RUN64]));
        if (configitem[TOOLEMBED_RUN64][TOOLEMBED_RUN_ARGS])
            config.ToEmbed.push_back(
                EmbeddedResource::EmbedSpec::AddRun64Args(configitem[TOOLEMBED_RUN64][TOOLEMBED_RUN_ARGS]));
    }

    return S_OK;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
    UtilitiesLoggerConfiguration::Parse(argc, argv, m_utilitiesConfig.log);

    HRESULT hr = E_FAIL;

    std::wstring strParameter;

    for (int i = 0; i < argc; i++)
    {
        switch (argv[i][0])
        {
            case L'/':
            case L'-':
                if (OptionalParameterOption(argv[i] + 1, L"Dump", strParameter))
                {
                    config.Todo = Main::Dump;
                    if (!strParameter.empty())
                        config.strInputFile = strParameter;
                }
                if (OptionalParameterOption(argv[i] + 1, L"FromDump", strParameter))
                {
                    config.Todo = Main::FromDump;
                    if (!strParameter.empty())
                        config.strInputFile = strParameter;
                }
                else if (InputFileOption(argv[i] + 1, L"Input", config.strInputFile))
                    ;
                else if (OutputOption(
                             argv[i] + 1,
                             L"Out",
                             static_cast<OutputSpec::Kind>(OutputSpec::Kind::File | OutputSpec::Kind::Directory),
                             config.Output))
                    ;
                else if (ParameterOption(argv[i] + 1, L"Run32", strParameter))
                {
                    if (EmbeddedResource::IsResourceBased(strParameter))
                        config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddRunX86(std::move(strParameter)));
                    else
                    {
                        Log::Error(L"Invalid ressource pattern specified: {}", strParameter);
                        return E_INVALIDARG;
                    }
                }
                else if (ParameterOption(argv[i] + 1, L"Run64", strParameter))
                {
                    if (EmbeddedResource::IsResourceBased(strParameter))
                        config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddRunX64(std::move(strParameter)));
                    else
                    {
                        Log::Error(L"Invalid ressource pattern specified: '{}'", strParameter);
                        return E_INVALIDARG;
                    }
                }
                else if (ParameterOption(argv[i] + 1, L"Run", strParameter))
                {
                    if (EmbeddedResource::IsResourceBased(strParameter))
                        config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddRun(std::move(strParameter)));
                    else
                    {
                        Log::Error(L"Invalid ressource pattern specified: '{}'", strParameter);
                        return E_INVALIDARG;
                    }
                }
                else if (ParameterOption(argv[i] + 1, L"AddFile", strParameter))
                {
                    static std::wregex r(L"([a-zA-Z0-9\\\\ \\-_\\.:%]*),([a-zA-Z0-9\\-_\\.]+)");
                    std::wsmatch s;

                    if (std::regex_match(strParameter, s, r))
                    {
                        WCHAR szFile[MAX_PATH];

                        if (FAILED(hr = ExpandFilePath(s[1].str().c_str(), szFile, MAX_PATH)))
                        {
                            Log::Error(L"Invalid file to embed specified: {}", strParameter);
                            return E_INVALIDARG;
                        }
                        if (s[2].matched)
                        {
                            config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddFile(s[2].str(), s[1].str()));
                        }
                    }
                    else
                    {
                        Log::Error(L"Option /AddFile should be like: /AddFile=File.cab,Name");
                        return E_INVALIDARG;
                    }
                }
                else if (ParameterOption(argv[i] + 1, L"Remove", strParameter))
                {
                    static std::wregex r(L"([a-zA-Z0-9\\-_\\.]*)");
                    std::wsmatch s;

                    if (std::regex_match(strParameter, s, r))
                    {
                        config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddBinaryDeletion(s[1].str()));
                    }
                    else
                    {
                        Log::Error("Option /Remove should be like: /Remove=101 or /Remove=ResourceCab");
                        return E_INVALIDARG;
                    }
                }
                else if (UsageOption(argv[i] + 1))
                    ;
                else if (!_wcsnicmp(argv[i] + 1, L"Config", wcslen(L"Config")))
                {
                    // simply avoiding a config=config.xml name value pair in the output
                }
                else if (IgnoreCommonOptions(argv[i] + 1))
                    ;
                else
                {
                    LPCWSTR pEquals = wcschr(argv[i] + 1, L'=');
                    if (pEquals)
                    {
                        static std::wregex r(L"([a-zA-Z0-9_]+)=(.*)");
                        std::wsmatch s;
                        std::wstring str(argv[i] + 1);

                        if (std::regex_match(str, s, r))
                        {
                            config.ToEmbed.push_back(
                                EmbeddedResource::EmbedSpec::AddNameValuePair(s[1].str(), s[2].str()));
                        }
                        else
                        {
                            Log::Error("Option should be like: /Name=Value");
                            return E_INVALIDARG;
                        }
                    }
                }
                break;
            default:
                break;
        }
    }
    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    HRESULT hr = E_FAIL;

    if (!m_utilitiesConfig.log.level && !m_utilitiesConfig.log.console.level)
    {
        m_utilitiesConfig.log.console.level = Log::Level::Info;
    }

    UtilitiesLoggerConfiguration::Apply(m_logging, m_utilitiesConfig.log);

    DWORD dwMajor = 0, dwMinor = 0;
    SystemDetails::GetOSVersion(dwMajor, dwMinor);

    if (dwMajor < 6 && dwMinor < 2)
    {
        Log::Error("ToolEmbed cannot be used on downlevel platform, please run ToolEmbed on Windows 7+ systems");
        return E_ABORT;
    }

    switch (config.Todo)
    {
        case Main::Embed:
            if (config.Output.Type != OutputSpec::Kind::File)
            {
                Log::Error("ToolEmbed can only embed into an output file");
                return E_INVALIDARG;
            }
            break;
        case Main::FromDump:
            if (config.Output.Type != OutputSpec::Kind::File)
            {
                Log::Error("ToolEmbed can only embed from a dump into an output file");
                return E_INVALIDARG;
            }
            if (!config.strConfigFile.empty())
            {
                Log::Error("ToolEmbed embedding from a dump cannot use a config file");
                return E_INVALIDARG;
            }
            break;
        case Main::Dump:
            if (!config.strInputFile.empty())
            {
                // /dump= requires the use of absolute paths
                WCHAR szFullPath[MAX_PATH] = {0};
                if (!GetFullPathName(config.strInputFile.c_str(), MAX_PATH, szFullPath, NULL))
                {
                    Log::Error(L"Failed to compute full path name for: '{}'", config.strInputFile);
                    return E_INVALIDARG;
                }
                config.strInputFile.assign(szFullPath);
            }
            if (config.Output.Type != OutputSpec::Kind::Directory)
            {
                Log::Error("ToolEmbed can only dump a configuration into a directory");
                return E_INVALIDARG;
            }
            break;
        default:
            break;
    }

    return S_OK;
}
