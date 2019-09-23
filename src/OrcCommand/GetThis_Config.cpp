//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "GetThis.h"

#include "ParameterCheck.h"
#include "FileFind.h"
#include "LogFileWriter.h"
#include "TableOutputWriter.h"

#include "ConfigFile_GetThis.h"

#include <boost/tokenizer.hpp>

#include <regex>
#include <filesystem>

using namespace std;
namespace fs = std::experimental::filesystem;

using namespace Orc;
using namespace Orc::Command::GetThis;

std::wregex Main::Configuration::g_ContentRegEx(L"(strings|data|raw)(:(min=([0-9]+))?,?(max=([0-9]+)))?");

constexpr auto REGEX_CONTENT_TYPE = 1;
constexpr auto REGEX_CONTENT_STRINGS_MIN = 4;
constexpr auto REGEX_CONTENT_STRINGS_MAX = 6;

constexpr auto CONTENT_STRINGS_DEFAULT_MIN = 3;
constexpr auto CONTENT_STRINGS_DEFAULT_MAX = 1024;

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaitem)
{
    config.Output.Schema = TableOutput::GetColumnsFromConfig(
        _L_, config.Output.TableKey.empty() ? L"getthis_collection" : config.Output.TableKey.c_str(), schemaitem);
    return S_OK;
}

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return Orc::Config::GetThis::root;
}

ContentSpec Main::Configuration::GetContentSpecFromString(const std::wstring& str)
{
    ContentSpec retval;
    wsmatch s_content;

    if (std::regex_match(str, s_content, g_ContentRegEx))
    {
        if (!s_content[REGEX_CONTENT_TYPE].compare(L"strings"))
        {
            retval.Type = STRINGS;
            if (s_content[REGEX_CONTENT_STRINGS_MIN].matched)
            {
                if (FAILED(GetIntegerFromArg(s_content[REGEX_CONTENT_STRINGS_MIN].str().c_str(), retval.MinChars)))
                {
                    retval.MinChars = CONTENT_STRINGS_DEFAULT_MIN;
                }
            }
            else
                retval.MinChars = CONTENT_STRINGS_DEFAULT_MIN;

            if (s_content[REGEX_CONTENT_STRINGS_MAX].matched)
            {
                if (FAILED(GetIntegerFromArg(s_content[REGEX_CONTENT_STRINGS_MAX].str().c_str(), retval.MaxChars)))
                {
                    retval.MaxChars = CONTENT_STRINGS_DEFAULT_MAX;
                }
            }
            else
                retval.MaxChars = CONTENT_STRINGS_DEFAULT_MAX;
        }
        else if (!s_content[REGEX_CONTENT_TYPE].compare(L"data"))
        {
            retval.Type = DATA;
        }
        else if (!s_content[REGEX_CONTENT_TYPE].compare(L"raw"))
        {
            retval.Type = RAW;
        }
        else
        {
            retval.Type = INVALID;
        }
    }

    return retval;
}

HRESULT Main::GetConfigurationFromConfig(const ConfigItem& configitem)
{
    HRESULT hr = E_FAIL;

    ConfigFile::ConfigureLogging(configitem.SubItems[GETTHIS_LOGGING], _L_);

    ConfigFile reader(_L_);

    if (FAILED(
            hr = config.Output.Configure(
                _L_,
                static_cast<OutputSpec::Kind>(OutputSpec::Kind::Archive | OutputSpec::Kind::Directory),
                configitem[GETTHIS_OUTPUT])))
    {
        return hr;
    }

    const ConfigItem& locationsConfig = configitem[GETTHIS_LOCATION];

    if (boost::logic::indeterminate(config.bAddShadows))
    {
        for (auto& item : locationsConfig.NodeList)
        {
            if (item.SubItems[CONFIG_VOLUME_SHADOWS])
            {
                config.bAddShadows = true;
            }
        }
    }

    if (FAILED(hr = config.Locations.AddLocationsFromConfigItem(configitem[GETTHIS_LOCATION])))
    {
        log::Error(_L_, E_FAIL, L"Syntax error in specific locations parsing in config file\r\n");
        return hr;
    }

    if (FAILED(hr = config.Locations.AddKnownLocations(configitem[GETTHIS_KNOWNLOCATIONS])))
    {
        log::Error(_L_, E_FAIL, L"Syntax error in known locations parsing in config file\r\n");
        return hr;
    }

    if (configitem[GETTHIS_SAMPLES][CONFIG_MAXBYTESPERSAMPLE])
    {
        config.limits.dwlMaxBytesPerSample = (DWORD64)configitem[GETTHIS_SAMPLES][CONFIG_MAXBYTESPERSAMPLE];
    }

    if (configitem[GETTHIS_SAMPLES][CONFIG_MAXBYTESTOTAL])
    {
        config.limits.dwlMaxBytesTotal = (DWORD64)configitem[GETTHIS_SAMPLES][CONFIG_MAXBYTESTOTAL];
    }

    if (configitem[GETTHIS_SAMPLES][CONFIG_MAXSAMPLECOUNT])
    {
        config.limits.dwMaxSampleCount = (DWORD)configitem[GETTHIS_SAMPLES][CONFIG_MAXSAMPLECOUNT];
    }
    if (configitem[GETTHIS_SAMPLES][CONFIG_SAMPLE_CONTENT])
    {
        config.content = config.GetContentSpecFromString(configitem[GETTHIS_SAMPLES][CONFIG_SAMPLE_CONTENT]);
    }

    std::for_each(
        begin(configitem[GETTHIS_SAMPLES][CONFIG_SAMPLE].NodeList),
        end(configitem[GETTHIS_SAMPLES][CONFIG_SAMPLE].NodeList),
        [this](const ConfigItem& item) {
            SampleSpec aSpec;

            if (item[CONFIG_SAMPLE_FILEFIND].NodeList.empty())
            {
                auto filespec = make_shared<FileFind::SearchTerm>(item.strData);
                filespec->Required = FileFind::SearchTerm::Criteria::NAME;
                aSpec.Terms.push_back(filespec);
            }
            else
            {
                for (const auto& finditem : item[CONFIG_SAMPLE_FILEFIND].NodeList)
                {
                    auto filespec = FileFind::GetSearchTermFromConfig(finditem);

                    if (const auto [valid, reason] = filespec->IsValidTerm(); valid)
                        aSpec.Terms.push_back(filespec);
                    else
                        log::Error(_L_, E_INVALIDARG, L"Term is invalid, reason: %s\r\n", reason.c_str());
                }
            }

            if (!item[CONFIG_SAMPLE_EXCLUDE].NodeList.empty())
            {
                for (const auto& finditem : item[CONFIG_SAMPLE_EXCLUDE].NodeList)
                {
                    auto filespec = FileFind::GetSearchTermFromConfig(finditem);
                    if (const auto [valid, reason] = filespec->IsValidTerm(); valid)
                        config.listOfExclusions.push_back(filespec);
                    else
                        log::Error(_L_, E_INVALIDARG, L"Term is invalid, reason: %s\r\n", reason.c_str());
                }
            }

            if (item[CONFIG_MAXBYTESPERSAMPLE])
            {
                aSpec.PerSampleLimits.dwlMaxBytesPerSample = (DWORD64)item[CONFIG_MAXBYTESPERSAMPLE];
            }
            if (item[CONFIG_MAXBYTESTOTAL])
            {
                aSpec.PerSampleLimits.dwlMaxBytesTotal = (DWORD64)item[CONFIG_MAXBYTESTOTAL];
            }
            if (item[CONFIG_MAXSAMPLECOUNT])
            {
                aSpec.PerSampleLimits.dwMaxSampleCount = (DWORD)item[CONFIG_MAXSAMPLECOUNT];
            }
            if (item[CONFIG_SAMPLE_NAME])
            {
                aSpec.Name = item[CONFIG_SAMPLE_NAME];
            }
            if (item[CONFIG_SAMPLE_CONTENT])
            {
                aSpec.Content = config.GetContentSpecFromString(item[CONFIG_SAMPLE_CONTENT]);
            }
            config.listofSpecs.push_back(std::move(aSpec));
        });

    if (!configitem[GETTHIS_SAMPLES][CONFIG_SAMPLE_EXCLUDE].NodeList.empty())
    {
        std::for_each(
            begin(configitem[GETTHIS_SAMPLES][CONFIG_SAMPLE_EXCLUDE].NodeList),
            end(configitem[GETTHIS_SAMPLES][CONFIG_SAMPLE_EXCLUDE].NodeList),
            [this](const ConfigItem& finditem) {
                auto filespec = FileFind::GetSearchTermFromConfig(finditem);
                config.listOfExclusions.push_back(filespec);
            });
    }

    if (configitem[GETTHIS_FLUSHREGISTRY])
    {
        using namespace std::string_view_literals;
        constexpr auto YES = L"Yes"sv;

        if (equalCaseInsensitive((const std::wstring&)configitem[GETTHIS_FLUSHREGISTRY], YES, YES.size()))
        {
            config.bFlushRegistry = true;
        }
        else
            config.bFlushRegistry = false;
    }

    if (configitem[GETTHIS_NOLIMITS])
    {
        config.limits.bIgnoreLimits = true;
    }

    if (configitem[GETTHIS_REPORTALL])
    {
        config.bReportAll = true;
    }
    if (configitem[GETTHIS_HASH])
    {
        config.CryptoHashAlgs =
            CryptoHashStream::GetSupportedAlgorithm(configitem[GETTHIS_FLUSHREGISTRY].strData.c_str());
    }
    if (configitem[GETTHIS_FUZZYHASH])
    {
        config.FuzzyHashAlgs = FuzzyHashStream::GetSupportedAlgorithm(configitem[GETTHIS_FUZZYHASH].strData.c_str());
    }
    if (configitem[GETTHIS_YARA])
    {
        config.Yara = std::make_unique<YaraConfig>(YaraConfig::Get(_L_, configitem[GETTHIS_YARA]));
    }
    return S_OK;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
    HRESULT hr = S_OK;
    std::wstring strContent;

    try
    {
        for (int i = 0; i < argc; i++)
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
                    else if (!_wcsnicmp(argv[i] + 1, L"Sample", wcslen(L"Sample")))
                    {
                        LPCWSTR pEquals = wcschr(argv[i], L'=');
                        if (!pEquals)
                        {
                            log::Error(_L_, E_INVALIDARG, L"Option /Sample should be like: /Sample=malware.exe\n");
                            return E_INVALIDARG;
                        }
                        else
                        {
                            SampleSpec aSpec;
                            auto filespec = make_shared<FileFind::SearchTerm>(wstring(pEquals + 1));
                            aSpec.Terms.push_back(filespec);
                            aSpec.Content.Type = INVALID;
                            config.listofSpecs.push_back(aSpec);
                        }
                    }
                    else if (BooleanOption(argv[i] + 1, L"FlushRegistry", config.bFlushRegistry))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"ReportAll", config.bReportAll))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"NoLimits", config.limits.bIgnoreLimits))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"Shadows", config.bAddShadows))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Password", config.Output.Password))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"NoLimits", config.limits.bIgnoreLimits))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Compression", config.Output.Compression))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Content", strContent))
                    {
                        config.content = config.GetContentSpecFromString(strContent);
                    }
                    else if (!_wcsnicmp(argv[i] + 1, L"XOR", wcslen(L"XOR")))
                    {
                        DWORD dwXOR = 0L;
                        LPCWSTR pEquals = wcschr(argv[i], L'=');
                        if (!pEquals)
                        {
                            log::Error(_L_, E_INVALIDARG, L"Option /XOR should be like: /XOR=0xBADF00D0\r\n");
                            return E_INVALIDARG;
                        }
                        else
                        {
                            if (FAILED(GetIntegerFromHexaString(pEquals + 1, dwXOR)))
                            {
                                log::Error(_L_, E_INVALIDARG, L"Invalid XOR pattern: %s\r\n", pEquals + 1);
                                return E_INVALIDARG;
                            }
                            config.Output.XOR = dwXOR;
                        }
                    }
                    else if (AltitudeOption(argv[i] + 1, L"Altitude", config.Locations.GetAltitude()))
                        ;
                    else if (CryptoHashAlgorithmOption(argv[i] + 1, L"Hash", config.CryptoHashAlgs))
                        ;
                    else if (FuzzyHashAlgorithmOption(argv[i] + 1, L"fuzzyhash", config.FuzzyHashAlgs))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Yara", config.YaraSource))
                    {
                        if (!config.Yara)
                            config.Yara = std::make_unique<YaraConfig>();
                        boost::split(config.Yara->Sources(), config.YaraSource, boost::is_any_of(";,"));
                    }
                    else if (EncodingOption(argv[i] + 1, config.Output.OutputEncoding))
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

        if (FAILED(hr = config.Locations.AddLocationsFromArgcArgv(argc, argv)))
        {
            log::Error(_L_, E_INVALIDARG, L"Error in specific locations parsing\r\n");
            return E_INVALIDARG;
        }
    }
    catch (...)
    {
        log::Error(_L_, E_INVALIDARG, L"GetThis failed during argument parsing, exiting\r\n");
        return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    HRESULT hr = E_FAIL;

    if (boost::logic::indeterminate(config.bAddShadows))
        config.bAddShadows = false;

    config.Locations.Consolidate((bool)config.bAddShadows, FSVBR::FSType::NTFS);

    if (config.Output.Type == OutputSpec::Kind::None || config.Output.Path.empty())
    {
        log::Info(_L_, L"\r\nINFO: Not output explicitely specified: creating GetThis.7z in current directory\r\n");
        config.Output.Path = L"GetThis.7z";
        config.Output.Type = OutputSpec::Kind::Archive;
        config.Output.ArchiveFormat = ArchiveFormat::SevenZip;
        config.Output.Compression = L"Normal";
    }

    if (!config.strExtractCab.empty())
    {
        if (config.Output.Path.empty())
        {
            fs::path aArchivePath(config.strExtractCab);
            fs::path aDir;

            aDir = aArchivePath.parent_path();
            aDir /= aArchivePath.stem();
            fs::create_directory(aDir);

            if (FAILED(hr = GetOutputDir(aDir.wstring().c_str(), config.Output.Path)))
            {
                log::Error(_L_, hr, L"Could not use %s as output dir\r\n", L"Defaut");
                return hr;
            }
            log::Info(_L_, L"Information: output directory omitted, using %s\r\n", config.Output.Path.c_str());
        }
    }

    if (config.content.Type == ContentType::INVALID)
    {
        config.content.Type = ContentType::DATA;
    }

    std::for_each(begin(config.listofSpecs), end(config.listofSpecs), [this](SampleSpec& aSpec) {
        if (aSpec.Content.Type == ContentType::INVALID)
        {
            aSpec.Content = config.content;
        }

        std::for_each(begin(aSpec.Terms), end(aSpec.Terms), [this](const shared_ptr<FileFind::SearchTerm>& filespec) {
            FileFinder.AddTerm(filespec);
        });
    });

    std::for_each(
        begin(config.listOfExclusions),
        end(config.listOfExclusions),
        [this](const std::shared_ptr<FileFind::SearchTerm>& aTerm) { FileFinder.AddExcludeTerm(aTerm); });

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

    GlobalLimits = config.limits;

    if (FAILED(hr = FileFinder.InitializeYara(config.Yara)))
    {
        log::Error(_L_, hr, L"Failed to initialize yara scanner\r\n");
        return hr;
    }

    if (FAILED(hr = FileFinder.CheckYara()))
        return hr;

    return S_OK;
}
