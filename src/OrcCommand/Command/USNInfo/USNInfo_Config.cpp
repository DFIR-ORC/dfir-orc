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

#include "Configuration/ConfigFile.h"
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
        Log::Error("Failed to get locations definition from config [{}]", SystemError(hr));
        return hr;
    }

    boost::logic::tribool bAddShadows;
    for (auto& item : configitem[USNINFO_LOCATIONS].NodeList)
    {
        if (item.SubItems[CONFIG_VOLUME_SHADOWS]  && !config.m_shadows.has_value())
        {
            ParseShadowOption(item.SubItems[CONFIG_VOLUME_SHADOWS], bAddShadows, config.m_shadows);
        }

        if (item.SubItems[CONFIG_VOLUME_EXCLUDE] && !config.m_excludes.has_value())
        {
            ParseLocationExcludes(item.SubItems[CONFIG_VOLUME_EXCLUDE], config.m_excludes);
        }
    }

    if (boost::logic::indeterminate(config.bAddShadows))
    {
        config.bAddShadows = bAddShadows;
    }

    if (FAILED(hr = config.locs.AddKnownLocations(configitem[USNINFO_KNOWNLOCATIONS])))
    {
        Log::Error("Failed to get known locations [{}]", SystemError(hr));
        return hr;
    }

    if (configitem[USNINFO_LOGGING])
    {
        Log::Warn(L"The '<logging> configuration element is deprecated, please use '<log>' instead");
    }

    if (configitem[USNINFO_LOG])
    {
        UtilitiesLoggerConfiguration::Parse(configitem[USNINFO_LOG], m_utilitiesConfig.log);
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

    UtilitiesLoggerConfiguration::Parse(argc, argv, m_utilitiesConfig.log);

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
                else if (ShadowsOption(argv[i] + 1, L"Shadows", config.bAddShadows, config.m_shadows))
                    ;
                else if (LocationExcludeOption(argv[i] + 1, L"Exclude", config.m_excludes))
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

    UtilitiesLoggerConfiguration::Apply(m_logging, m_utilitiesConfig.log);

    if (m_utilitiesConfig.strComputerName.empty())
    {
        SystemDetails::GetOrcComputerName(m_utilitiesConfig.strComputerName);
    }

    if (boost::logic::indeterminate(config.bAddShadows))
    {
        config.bAddShadows = false;
    }

    config.locs.Consolidate(
        static_cast<bool>(config.bAddShadows),
        config.m_shadows.value_or(LocationSet::ShadowFilters()),
        config.m_excludes.value_or(LocationSet::PathExcludes()),
        FSVBR::FSType::NTFS);

    if (config.locs.IsEmpty() != S_OK)
    {
        Log::Critical(
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
            Log::Critical(
                L"Specified file information output directory '{}' is not a directory [{}]",
                config.output.Path,
                SystemError(hr));
            return hr;
        }
    }

    if (!config.output.Schema)
    {
        Log::Warn("Sql Schema is empty");
    }

    return S_OK;
}
