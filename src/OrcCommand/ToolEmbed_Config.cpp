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
#include "LogFileWriter.h"

#include "ConfigFile_ToolEmbed.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command::ToolEmbed;

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return Orc::Config::ToolEmbed::root;
}

HRESULT
Main::GetNameValuePairFromConfigItem(const logger& pLog, const ConfigItem& item, EmbeddedResource::EmbedSpec& spec)
{
    if (wcscmp(item.strName.c_str(), L"pair"))
    {
        log::Verbose(pLog, L"item passed is not a name value pair\r\n");
        return E_FAIL;
    }

    if (!item[TOOLEMBED_PAIRNAME])
    {
        log::Verbose(pLog, L"name attribute is missing in name value pair\r\n");
        return E_FAIL;
    }

    if (!item[TOOLEMBED_PAIRVALUE])
    {
        log::Verbose(pLog, L"value attribute is missing in name value pair\r\n");
        return E_FAIL;
    }

    std::swap(
        spec,
        EmbeddedResource::EmbedSpec::AddNameValuePair(
            item[TOOLEMBED_PAIRNAME], item[TOOLEMBED_PAIRVALUE]));

    return S_OK;
}

HRESULT Main::GetAddFileFromConfigItem(const logger& pLog, const ConfigItem& item, EmbeddedResource::EmbedSpec& spec)
{
    if (wcscmp(item.strName.c_str(), L"file"))
    {
        log::Verbose(pLog, L"item passed is not a file addition request\r\n");
        return E_FAIL;
    }

    if (!item[TOOLEMBED_FILENAME])
    {
        log::Verbose(pLog, L"name attribute is missing in file addition request\r\n");
        return E_FAIL;
    }

    if (!item[TOOLEMBED_FILEPATH])
    {
        log::Verbose(pLog, L"path attribute is missing in file addition request\r\n");
        return E_FAIL;
    }

    wstring strInputFile;

    if (item)
    {
        HRESULT hr = E_FAIL;
        if (FAILED(hr = GetInputFile(((const std::wstring&)item[TOOLEMBED_FILEPATH]).c_str(), strInputFile)))
        {
            log::Error(
                pLog,
                hr,
                L"Error in specified file (%s) to add in config file\r\n",
                item[TOOLEMBED_FILEPATH].c_str());
            return hr;
        }
    }

    std::swap(
        spec,
        EmbeddedResource::EmbedSpec::AddFile((const std::wstring&)item[TOOLEMBED_FILENAME], std::move(strInputFile)));

    return S_OK;
}

HRESULT Main::GetAddArchiveFromConfigItem(const logger& pLog, const ConfigItem& item, EmbeddedResource::EmbedSpec& spec)
{
    HRESULT hr = E_FAIL;

    if (wcscmp(item.strName.c_str(), L"archive"))
    {
        log::Verbose(pLog, L"item passed is not a archive addition request\r\n");
        return E_FAIL;
    }

    if (!item[TOOLEMBED_ARCHIVE_NAME])
    {
        log::Verbose(pLog, L"name attribute is missing in archive addition request\r\n");
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
        log::Verbose(pLog, L"files to archive items are missing in archive addition request\r\n");
        return E_FAIL;
    }

    std::vector<EmbeddedResource::EmbedSpec::ArchiveItem> items;

    hr = S_OK;
    std::for_each(
        begin(item[TOOLEMBED_FILE2ARCHIVE].NodeList),
        end(item[TOOLEMBED_FILE2ARCHIVE].NodeList),
        [&pLog, &items, &hr](const ConfigItem& item2cab) {
            EmbeddedResource::EmbedSpec::ArchiveItem toCab;

            toCab.Name = item2cab[TOOLEMBED_FILE2ARCHIVE_NAME];

            wstring strInputFile;

            HRESULT subhr = E_FAIL;
            if (FAILED(subhr = GetInputFile(item2cab[TOOLEMBED_FILE2ARCHIVE_PATH].c_str(), strInputFile)))
            {
                log::Error(
                    pLog,
                    subhr,
                    L"Error in specified file (%s) to add to cab file\r\n",
                    item2cab[TOOLEMBED_FILE2ARCHIVE_PATH].c_str());
                hr = subhr;
                return;
            }

            std::swap(toCab.Path, strInputFile);

            items.push_back(toCab);
        });

    if (FAILED(hr))
        return hr;

    std::swap(
        spec,
        EmbeddedResource::EmbedSpec::AddArchive(
            std::move(strArchiveName),
            std::move(strArchiveFormat),
            std::move(strArchiveCompression),
            std::move(items)));

    return S_OK;
}

HRESULT Main::GetConfigurationFromConfig(const ConfigItem& configitem)
{
    HRESULT hr = E_FAIL;

    ConfigFile reader(_L_);

    if (FAILED(hr = reader.ConfigureLogging(configitem[TOOLEMBED_LOGGING], _L_)))
        return hr;

    {
        if (FAILED(hr = reader.GetInputFile(configitem[TOOLEMBED_INPUT], config.strInputFile)))
            return hr;
    }

    {
        if (FAILED(
                hr = config.Output.Configure(
                    _L_,
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

                if (SUCCEEDED(local_hr = GetNameValuePairFromConfigItem(_L_, item, spec)))
                {
                    if (spec.Type != EmbeddedResource::EmbedSpec::Void)
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

                if (SUCCEEDED(local_hr = GetAddFileFromConfigItem(_L_, item, spec)))
                {
                    if (spec.Type != EmbeddedResource::EmbedSpec::Void)
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

                if (SUCCEEDED(local_hr = GetAddArchiveFromConfigItem(_L_, item, spec)))
                {
                    if (spec.Type != EmbeddedResource::EmbedSpec::Void)
                        config.ToEmbed.push_back(std::move(spec));
                }
                else
                    hr = local_hr;
            });
    }

    if (FAILED(hr))
        return hr;

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
                        log::Error(
                            _L_, E_INVALIDARG, L"Invalid ressource pattern specified: %s\r\n", strParameter.c_str());
                        return E_INVALIDARG;
                    }
                }
                else if (ParameterOption(argv[i] + 1, L"Run64", strParameter))
                {
                    if (EmbeddedResource::IsResourceBased(strParameter))
                        config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddRunX64(std::move(strParameter)));
                    else
                    {
                        log::Error(
                            _L_, E_INVALIDARG, L"Invalid ressource pattern specified: %s\r\n", strParameter.c_str());
                        return E_INVALIDARG;
                    }
                }
                else if (ParameterOption(argv[i] + 1, L"Run", strParameter))
                {
                    if (EmbeddedResource::IsResourceBased(strParameter))
                        config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddRun(std::move(strParameter)));
                    else
                    {
                        log::Error(
                            _L_, E_INVALIDARG, L"Invalid ressource pattern specified: %s\r\n", strParameter.c_str());
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

                        if (FAILED(hr = GetInputFile(s[1].str().c_str(), szFile, MAX_PATH)))
                        {
                            log::Error(_L_, hr, L"Invalid file to embed specified: %s\r\n", strParameter.c_str());
                            return E_INVALIDARG;
                        }
                        if (s[2].matched)
                        {
                            config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddFile(s[2].str(), s[1].str()));
                        }
                    }
                    else
                    {
                        log::Error(_L_, E_INVALIDARG, L"Option /AddFile should be like: /AddFile=File.cab,Name\r\n");
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
                        log::Error(
                            _L_,
                            E_INVALIDARG,
                            L"Option /Remove should be like: /Remove=101 or /Remove=ResourceCab\r\n");
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
                            log::Error(_L_, E_INVALIDARG, L"Option should be like: /Name=Value\r\n");
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

    DWORD dwMajor = 0, dwMinor = 0;
    SystemDetails::GetOSVersion(dwMajor, dwMinor);

    if (dwMajor < 6 && dwMinor < 2)
    {
        log::Error(
            _L_,
            E_ABORT,
            L"ToolEmbed cannot be used on downlevel platform, please run ToolEmbed on Windows 7+ systems\r\n");
        return E_ABORT;
    }

    switch (config.Todo)
    {
        case Main::Embed:
            if (config.Output.Type != OutputSpec::File)
            {
                log::Error(_L_, E_INVALIDARG, L"ToolEmbed can only embed into an output file\r\n");
                return E_INVALIDARG;
            }
            break;
        case Main::FromDump:
            if (config.Output.Type != OutputSpec::File)
            {
                log::Error(_L_, E_INVALIDARG, L"ToolEmbed can only embed from a dump into an output file\r\n");
                return E_INVALIDARG;
            }
            if (!config.strConfigFile.empty())
            {
                log::Error(_L_, E_INVALIDARG, L"ToolEmbed embedding from a dump cannot use a config file\r\n");
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
                    log::Error(
                        _L_,
                        E_INVALIDARG,
                        L"Failed to compute full path name for path %s\r\n",
                        config.strInputFile.c_str());
                    return E_INVALIDARG;
                }
                config.strInputFile.assign(szFullPath);
            }
            if (config.Output.Type != OutputSpec::Directory)
            {
                log::Error(_L_, E_INVALIDARG, L"ToolEmbed can only dump a configuration into a directory\r\n");
                return E_INVALIDARG;
            }
            break;
        default:
            break;
    }

    return S_OK;
}
