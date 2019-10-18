//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "ParameterCheck.h"
#include "ConfigFileReader.h"
#include "TableOutputWriter.h"
#include "SystemDetails.h"

#include "GetSamples.h"

#include "Temporary.h"

#include "ConfigFile_GetSamples.h"

#include "LogFileWriter.h"

using namespace Orc;
using namespace Orc::Command::GetSamples;

namespace {

void CheckGetThisConfiguration(Orc::Command::GetSamples::Main::Configuration& config)
{
    if (config.getthisName.empty())
    {
        config.getthisName = L"getthis.exe";
    }

    if (config.getthisRef.empty())
    {
        config.getthisRef = L"self:#";
    }

    const std::wstring_view getThisCmd(L"getthis");
    if (config.getthisArgs.empty())
    {
        config.getthisArgs = getThisCmd;
    }
    else if (!equalCaseInsensitive(config.getthisArgs, getThisCmd, getThisCmd.length()))
    {
        // Avoid having to specify 'getthis' as first argument
        config.getthisArgs.insert(0, getThisCmd);
        config.getthisArgs.insert(getThisCmd.size(), L" ");
    }
}

}  // namespace

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return Orc::Config::GetSamples::root;
}

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaitem)
{
    config.sampleinfoOutput.Schema = TableOutput::GetColumnsFromConfig(
        _L_,
        config.sampleinfoOutput.TableKey.empty() ? L"SampleInfo" : config.sampleinfoOutput.TableKey.c_str(),
        schemaitem);
    config.timelineOutput.Schema = TableOutput::GetColumnsFromConfig(
        _L_,
        config.timelineOutput.TableKey.empty() ? L"SampleTimeline" : config.timelineOutput.TableKey.c_str(),
        schemaitem);
    return S_OK;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, const WCHAR* argv[])
{
    HRESULT hr = E_FAIL;

    for (int i = 0; i < argc; i++)
    {
        switch (argv[i][0])
        {
            case L'/':
            case L'-':
                if (OutputOption(argv[i] + 1, L"GetThisConfig", OutputSpec::File, config.getThisConfig))
                    ;
                else if (ParameterOption(argv[i] + 1, L"GetThisArgs", config.getthisArgs))
                    ;
                else if (OutputOption(
                             argv[i] + 1,
                             L"Out",
                             static_cast<OutputSpec::Kind>(OutputSpec::Archive | OutputSpec::Directory),
                             config.samplesOutput))
                    ;
                else if (!_wcsnicmp(argv[i] + 1, L"Autoruns", wcslen(L"Autoruns")))
                {
                    LPCWSTR pEquals = wcschr(argv[i], L'=');
                    if (!pEquals)
                    {
                        if (BooleanOption(argv[i] + 1, L"Autoruns", config.bRunAutoruns))
                            continue;
                    }
                    else
                    {
                        if (SUCCEEDED(GetInputFile(pEquals + 1, config.autorunsOutput.Path)))
                        {
                            config.bLoadAutoruns = true;
                            config.bKeepAutorunsXML = false;
                        }
                        else
                        {
                            if (FAILED(config.autorunsOutput.Configure(_L_, OutputSpec::File, pEquals + 1)))
                            {
                                log::Error(_L_, E_INVALIDARG, L"Invalid autoruns file specified: %s\r\n", pEquals + 1);
                                return E_INVALIDARG;
                            }
                            else
                            {
                                config.bRunAutoruns = true;
                                config.bKeepAutorunsXML = true;
                            }
                        }
                    }
                }
                else if (OutputOption(argv[i] + 1, L"SampleInfo", OutputSpec::TableFile, config.sampleinfoOutput))
                    ;
                else if (OutputOption(argv[i] + 1, L"TimeLine", OutputSpec::TableFile, config.timelineOutput))
                    ;
                else if (OutputOption(argv[i] + 1, L"TempDir", OutputSpec::Directory, config.tmpdirOutput))
                    ;
                else if (FileSizeOption(argv[i] + 1, L"MaxPerSampleBytes", config.limits.dwlMaxBytesPerSample))
                    ;
                else if (FileSizeOption(argv[i] + 1, L"MaxTotalBytes", config.limits.dwlMaxBytesTotal))
                    ;
                else if (ParameterOption(argv[i] + 1, L"MaxSampleCount", config.limits.dwMaxSampleCount))
                    ;
                else if (BooleanOption(argv[i] + 1, L"NoLimits", config.limits.bIgnoreLimits))
                    ;
                else if (ParameterOption(argv[i] + 1, L"Compression", config.samplesOutput.Compression))
                    ;
                else if (ParameterOption(argv[i] + 1, L"Password", config.samplesOutput.Password))
                    ;
                else if (BooleanOption(argv[i] + 1, L"NoSigCheck", config.bNoSigCheck))
                    ;
                else if (EncodingOption(argv[i] + 1, config.csvEncoding))
                {
                    config.sampleinfoOutput.OutputEncoding = config.csvEncoding;
                    config.timelineOutput.OutputEncoding = config.csvEncoding;
                }
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
    return S_OK;
}

HRESULT Main::GetConfigurationFromConfig(const ConfigItem& configitem)
{
    HRESULT hr = E_FAIL;

    ConfigFile reader(_L_);

    if (FAILED(
            hr = config.samplesOutput.Configure(
                _L_,
                static_cast<OutputSpec::Kind>(OutputSpec::Archive | OutputSpec::Directory),
                configitem[GETSAMPLES_OUTPUT])))
        return hr;
    if (FAILED(
            hr =
                config.sampleinfoOutput.Configure(_L_, OutputSpec::Kind::TableFile, configitem[GETSAMPLES_SAMPLEINFO])))
        return hr;
    if (FAILED(hr = config.timelineOutput.Configure(_L_, OutputSpec::Kind::TableFile, configitem[GETSAMPLES_TIMELINE])))
        return hr;
    if (FAILED(hr = config.autorunsOutput.Configure(_L_, OutputSpec::Kind::File, configitem[GETSAMPLES_AUTORUNS])))
        return hr;

    if (config.autorunsOutput.Type != OutputSpec::None)
    {
        if (SUCCEEDED(VerifyFileExists(config.autorunsOutput.Path.c_str())))
        {
            config.bLoadAutoruns = true;
            config.bRunAutoruns = false;
            config.bKeepAutorunsXML = false;
        }
        else
        {
            config.bRunAutoruns = true;
            config.bLoadAutoruns = false;
            config.bKeepAutorunsXML = true;
        }
    }
    else
    {
        config.bRunAutoruns = false;
        config.bLoadAutoruns = false;
        config.bKeepAutorunsXML = false;
    }

    if (FAILED(hr = config.getThisConfig.Configure(_L_, OutputSpec::File, configitem[GETSAMPLES_GETTHIS_CONFIG])))
        return hr;

    if (configitem[GETSAMPLES_SAMPLES][CONFIG_MAXBYTESPERSAMPLE])
    {
        config.limits.dwlMaxBytesPerSample = (DWORD64)configitem[GETSAMPLES_SAMPLES][CONFIG_MAXBYTESPERSAMPLE];
    }

    if (configitem[GETSAMPLES_SAMPLES][CONFIG_MAXBYTESTOTAL])
    {
        config.limits.dwlMaxBytesTotal = (DWORD64)configitem[GETSAMPLES_SAMPLES][CONFIG_MAXBYTESTOTAL];
    }

    if (configitem[GETSAMPLES_SAMPLES][CONFIG_MAXSAMPLECOUNT])
    {
        config.limits.dwMaxSampleCount = (DWORD32)configitem[GETSAMPLES_SAMPLES][CONFIG_MAXSAMPLECOUNT];
    }

    if (configitem[GETSAMPLES_NOLIMITS])
    {
        config.limits.bIgnoreLimits = true;
    }

    if (configitem[GETSAMPLES_NOSIGCHECK])
    {
        config.bNoSigCheck = true;
    }

    if (configitem[GETSAMPLES_GETTHIS])
    {
        if (configitem[GETSAMPLES_GETTHIS][GETSAMPLES_GETTHIS_EXENAME])
        {
            config.getthisName = configitem[GETSAMPLES_GETTHIS][GETSAMPLES_GETTHIS_EXENAME];
        }

        WORD wArch = 0;
        if (FAILED(hr = SystemDetails::GetArchitecture(wArch)))
            return hr;

        switch (wArch)
        {
            case PROCESSOR_ARCHITECTURE_INTEL:
                if (configitem[GETSAMPLES_GETTHIS][GETSAMPLES_GETTHIS_EXERUN32])
                {
                    config.getthisRef = configitem[GETSAMPLES_GETTHIS][GETSAMPLES_GETTHIS_EXERUN32];
                }
                break;
            case PROCESSOR_ARCHITECTURE_AMD64:
                if (SystemDetails::IsWOW64())
                {
                    if (configitem[GETSAMPLES_GETTHIS][GETSAMPLES_GETTHIS_EXERUN32])
                    {
                        config.getthisRef = configitem[GETSAMPLES_GETTHIS][GETSAMPLES_GETTHIS_EXERUN32];
                    }
                }
                else
                {
                    if (configitem[GETSAMPLES_GETTHIS][GETSAMPLES_GETTHIS_EXERUN64])
                    {
                        config.getthisRef = configitem[GETSAMPLES_GETTHIS][GETSAMPLES_GETTHIS_EXERUN64];
                    }
                }
                break;
            default:
                log::Error(_L_, hr, L"Unsupported architechture %d\r\n", wArch);
                return hr;
        }

        if (config.getthisRef.empty())
        {
            if (configitem[GETSAMPLES_GETTHIS][GETSAMPLES_GETTHIS_EXERUN])
            {
                config.getthisRef = configitem[GETSAMPLES_GETTHIS][GETSAMPLES_GETTHIS_EXERUN];
            }
        }

        if (configitem[GETSAMPLES_GETTHIS][GETSAMPLES_GETTHIS_ARGS])
        {
            config.getthisArgs = configitem[GETSAMPLES_GETTHIS][GETSAMPLES_GETTHIS_ARGS];
        }
    }

    if (FAILED(hr = config.locs.AddLocationsFromConfigItem(configitem[GETSAMPLES_LOCATIONS])))
    {
        log::Error(_L_, hr, L"Error in specific locations parsing in config file\r\n");
        return hr;
    }

    if (FAILED(hr = config.locs.AddKnownLocations(configitem[GETSAMPLES_KNOWNLOCATIONS])))
    {
        log::Error(_L_, hr, L"Error in known locations parsing\r\n");
        return hr;
    }

    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    HRESULT hr = E_FAIL;

    // TODO: make a function to use also in GetThis_config.cpp
    if (!config.limits.bIgnoreLimits
        && (config.limits.dwlMaxBytesTotal == INFINITE && config.limits.dwMaxSampleCount == INFINITE))
    {
        log::Error(
            _L_,
            E_INVALIDARG,
            L"No global (at samples level, MaxBytesTotal or MaxSampleCount) has been set: set limits in configuration "
            L"or use /nolimits\r\n");
        return E_INVALIDARG;
    }

    CheckGetThisConfiguration(config);

    if (config.bInstallNTrack && config.bRemoveNTrack)
    {
        log::Error(_L_, E_FAIL, L"Cannot install and remove NTrack in same command\r\n");
        return E_FAIL;
    }

    if (config.tmpdirOutput.Path.empty())
    {
        WCHAR szTempDir[MAX_PATH];
        if (FAILED(hr = UtilGetTempDirPath(szTempDir, MAX_PATH)))
        {
            log::Error(_L_, hr, L"Failed to determine default temp folder\r\n");
            return hr;
        }
        config.tmpdirOutput.Path = szTempDir;
        config.tmpdirOutput.Type = OutputSpec::Directory;
    }

    // we support only NTFS for now to get samples
    config.locs.Consolidate(false, FSVBR::FSType::NTFS);

    return S_OK;
}
