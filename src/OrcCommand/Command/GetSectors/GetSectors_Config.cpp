//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jeremie Christin
//

#include "stdafx.h"
#include "Robustness.h"
#include "ParameterCheck.h"
#include "GetSectors.h"

#include <filesystem>

using namespace Orc;
using namespace Orc::Command::GetSectors;

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return nullptr;
}

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaitem)
{
    config.Output.Schema = TableOutput::GetColumnsFromConfig(
        config.Output.TableKey.empty() ? L"GetSectors" : config.Output.TableKey.c_str(), schemaitem);
    return S_OK;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
    UtilitiesLoggerConfiguration::Parse(argc, argv, m_utilitiesConfig.log);

    for (int i = 1; i < argc; i++)
    {
        switch (argv[i][0])
        {
            case L'/':
            case L'-':
                if (OutputOption(
                        argv[i] + 1,
                        L"Out",
                        static_cast<OutputSpec::Kind>(OutputSpec::Kind::Archive | OutputSpec::Kind::Directory),
                        config.Output))
                    ;
                else if (ParameterOption(argv[i] + 1, L"Disk", config.diskName))
                    ;
                else if (BooleanOption(argv[i] + 1, L"LegacyBootCode", config.dumpLegacyBootCode))
                    ;
                else if (FileSizeOption(argv[i] + 1, L"UefiFullMaxSize", config.uefiFullMaxSize))
                    ;
                else if (BooleanOption(argv[i] + 1, L"UefiFull", config.dumpUefiFull))
                    ;
                else if (FileSizeOption(argv[i] + 1, L"SlackSpaceDumpSize", config.slackSpaceDumpSize))
                    ;
                else if (BooleanOption(argv[i] + 1, L"SlackSpace", config.dumpSlackSpace))
                    ;
                else if (ParameterOption(argv[i] + 1, L"CustomOffset", config.customSampleOffset))
                    ;
                else if (ParameterOption(argv[i] + 1, L"CustomSize", config.customSampleSize))
                    ;
                else if (BooleanOption(argv[i] + 1, L"Custom", config.customSample))
                    ;
                else if (ToggleBooleanOption(argv[i] + 1, L"NotLowInterface", config.lowInterface))
                    ;
                else if (UsageOption(argv[i] + 1))
                    ;
                else if (IgnoreCommonOptions(argv[i] + 1))
                    ;
                else
                {
                    Log::Error(L"Failed to parse command line item: '{}'", argv[i] + 1);
                    PrintUsage();
                    return E_INVALIDARG;
                }
                break;
            default:
                config.diskName.assign(argv[i]);
                break;
        }
    }
    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    UtilitiesLoggerConfiguration::Apply(m_logging, m_utilitiesConfig.log);

    if ((!config.customSample) && (!config.dumpLegacyBootCode) && (!config.dumpSlackSpace) && (!config.dumpUefiFull))
    {
        Log::Error(L"[!] You must specify something to dump. Use /help to list the available options.");
        return E_FAIL;
    }

    if (config.diskName.empty())
    {
        // If no disk is specified, set it to the windows boot disk
        config.diskName = getBootDiskName();
    }

    if (config.Output.Type == OutputSpec::Kind::None || config.Output.Path.empty())
    {
        Log::Warn(L"INFO: No output explicitely specified: creating GetSectors.7z in current directory");
        config.Output.Path = L"GetSectors.7z";
        config.Output.Type = OutputSpec::Kind::Archive;
        config.Output.ArchiveFormat = ArchiveFormat::SevenZip;
        config.Output.Compression = L"Normal";
    }

    return S_OK;
}
