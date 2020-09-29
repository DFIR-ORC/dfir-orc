//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <vector>
#include <algorithm>

#include "USNInfo.h"

#include "ParameterCheck.h"
#include "SystemDetails.h"
#include "TableOutputWriter.h"

#include "ConfigFile.h"
#include "ConfigFile_USNInfo.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command::USNInfo;

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return Orc::Config::USNInfo::root;
}

HRESULT Main::GetConfigurationFromConfig(const ConfigItem& configitem)
{
    HRESULT hr = E_FAIL;

    ConfigFile reader;

    if (FAILED(hr = config.output.Configure(configitem[USNINFO_OUTPUT])))
        return hr;

    if (FAILED(hr = config.locs.AddLocationsFromConfigItem(configitem[USNINFO_LOCATIONS])))
    {
        Log::Error("Failed to get locations definition from config (code: {:#x})", hr);
        return hr;
    }
    if (FAILED(hr = config.locs.AddKnownLocations(configitem[USNINFO_KNOWNLOCATIONS])))
    {
        Log::Error("Failed to get known locations (code: {:#x})", hr);
        return hr;
    }

    config.bCompactForm = false;
    if (configitem[USNINFO_COMPACT])
        config.bCompactForm = true;

    return S_OK;
}

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaitem)
{
    config.output.Schema = TableOutput::GetColumnsFromConfig(
        config.output.TableKey.empty() ? L"USNInfo" : config.output.TableKey.c_str(), schemaitem);
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
                if (OutputOption(argv[i] + 1, L"out", config.output))
                    ;
                else if (BooleanOption(argv[i] + 1, L"Compact", config.bCompactForm))
                    ;
                else if (BooleanOption(argv[i] + 1, L"Shadows", config.bAddShadows))
                    ;
                else if (EncodingOption(argv[i] + 1, config.output.OutputEncoding))
                    ;
                else if (AltitudeOption(argv[i] + 1, L"Altitude", config.locs.GetAltitude()))
                    ;
                else if (ProcessPriorityOption(argv[i] + 1))
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
                break;
        }
    }

    if (FAILED(hr = config.locs.AddLocationsFromArgcArgv(argc, argv)))
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

    config.locs.Consolidate(config.bAddShadows, FSVBR::FSType::NTFS);

    if (config.locs.IsEmpty() != S_OK)
    {
        Log::Error(
            "No NTFS volumes configured for parsing. Use \"*\" to parse all mounted volumes or list the volumes you "
            "want parsed");
        return E_INVALIDARG;
    }

    if (config.output.Type == OutputSpec::Kind::None)
    {
        config.output.Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::CSV);
        config.output.szQuote = L"\"";
        config.output.szSeparator = L",";
        config.output.Path = L"USNInfo.csv";
        config.output.OutputEncoding = OutputSpec::Encoding::UTF8;
    }

    if (config.output.Type == OutputSpec::Kind::Directory)
    {
        if (FAILED(hr = ::VerifyDirectoryExists(config.output.Path.c_str())))
        {
            Log::Error(
                L"Specified file information output directory '{}' is not a directory (code: {:#x})",
                config.output.Path,
                hr);
            return hr;
        }
    }

    if (!config.output.Schema)
    {
        Log::Warn("Sql Schema is empty");
    }

    return S_OK;
}
