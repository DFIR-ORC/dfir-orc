//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "GetThis.h"

#include "ParameterCheck.h"
#include "FileFind.h"
#include "TableOutputWriter.h"

#include "ConfigFile_GetThis.h"

#include <boost/tokenizer.hpp>
#include <boost/algorithm/string.hpp>

#include <regex>
#include <filesystem>

using namespace std;
namespace fs = std::filesystem;

using namespace Orc;
using namespace Orc::Command::GetThis;

std::wregex Main::Configuration::g_ContentRegEx(L"(strings|data|raw)(:(min=([0-9]+))?,?(max=([0-9]+)))?");

constexpr auto REGEX_CONTENT_TYPE = 1;
constexpr auto REGEX_CONTENT_STRINGS_MIN = 4;
constexpr auto REGEX_CONTENT_STRINGS_MAX = 6;

constexpr auto CONTENT_STRINGS_DEFAULT_MIN = 3;
constexpr auto CONTENT_STRINGS_DEFAULT_MAX = 1024;

namespace {

void InitializeStatisticsOutput(Orc::Command::GetThis::Main::Configuration& config)
{
    std::filesystem::path outputDirectory;
    if (config.Output.IsFile())
    {
        outputDirectory = std::filesystem::path(config.Output.Path).parent_path();
    }
    else
    {
        outputDirectory = std::filesystem::path(config.Output.Path);
    }

    config.m_statisticsOutput.Path = (outputDirectory / L"statistics.json").c_str();
    config.m_statisticsOutput.Type = OutputSpec::Kind::File;
    config.m_statisticsOutput.OutputEncoding = OutputSpec::Encoding::UTF8;
}

}  // namespace

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaitem)
{
    config.Output.Schema = TableOutput::GetColumnsFromConfig(
        config.Output.TableKey.empty() ? L"getthis_collection" : config.Output.TableKey.c_str(), schemaitem);
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
            retval.Type = ContentType::STRINGS;
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
            retval.Type = ContentType::DATA;
        }
        else if (!s_content[REGEX_CONTENT_TYPE].compare(L"raw"))
        {
            retval.Type = ContentType::RAW;
        }
        else
        {
            retval.Type = ContentType::INVALID;
        }
    }

    return retval;
}

HRESULT Main::GetConfigurationFromConfig(const ConfigItem& configitem)
{
    HRESULT hr = E_FAIL;

    ConfigFile reader;

    if (configitem[GETTHIS_LOGGING])
    {
        Log::Warn(L"The '<logging> configuration element is deprecated, please use '<log>' instead");
    }

    if (configitem[GETTHIS_LOG])
    {
        UtilitiesLoggerConfiguration::Parse(configitem[GETTHIS_LOG], m_utilitiesConfig.log);
    }

    if (FAILED(
            hr = config.Output.Configure(
                static_cast<OutputSpec::Kind>(OutputSpec::Kind::Archive | OutputSpec::Kind::Directory),
                configitem[GETTHIS_OUTPUT])))
    {
        return hr;
    }

    const ConfigItem& locationsConfig = configitem[GETTHIS_LOCATION];

    boost::logic::tribool bAddShadows;
    for (auto& item : locationsConfig.NodeList)
    {
        if (item.SubItems[CONFIG_VOLUME_SHADOWS] && !config.m_shadows.has_value())
        {
            ParseShadowsOption(item.SubItems[CONFIG_VOLUME_SHADOWS], bAddShadows, config.m_shadows);
        }

        if (item.SubItems[CONFIG_VOLUME_SHADOWS_PARSER] && !config.m_shadowsParser.has_value())
        {
            std::error_code ec;
            ParseShadowsParserOption(item.SubItems[CONFIG_VOLUME_SHADOWS_PARSER], config.m_shadowsParser, ec);
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

    if (configitem[GETTHIS_LOCATION])
    {
        if (FAILED(hr = config.Locations.AddLocationsFromConfigItem(configitem[GETTHIS_LOCATION])))
        {
            Log::Error(L"Syntax error in specific locations parsing in config file");
            return hr;
        }

        LocationSet::ParseLocationsFromConfigItem(configitem[GETTHIS_LOCATION], config.inputLocations);
    }

    if (configitem[GETTHIS_KNOWNLOCATIONS])
    {
        if (FAILED(hr = config.Locations.AddKnownLocations(configitem[GETTHIS_KNOWNLOCATIONS])))
        {
            Log::Error(L"Syntax error in known locations parsing in config file");
            return hr;
        }

        LocationSet::ParseLocationsFromConfigItem(configitem[GETTHIS_KNOWNLOCATIONS], config.inputLocations);
    }

    if (configitem[GETTHIS_SAMPLES][CONFIG_MAXBYTESPERSAMPLE])
    {
        config.limits.dwlMaxBytesPerSample = (DWORD64)configitem[GETTHIS_SAMPLES][CONFIG_MAXBYTESPERSAMPLE];
    }

    if (configitem[GETTHIS_SAMPLES][CONFIG_MAXTOTALBYTES])
    {
        config.limits.dwlMaxTotalBytes = (DWORD64)configitem[GETTHIS_SAMPLES][CONFIG_MAXTOTALBYTES];
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
                auto filespec = make_shared<FileFind::SearchTerm>(item);
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
                        Log::Error(L"Term is invalid, reason: {}", reason);
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
                        Log::Error(L"Term is invalid, reason: {}", reason);
                }
            }

            if (item[CONFIG_MAXBYTESPERSAMPLE])
            {
                aSpec.PerSampleLimits.dwlMaxBytesPerSample = (DWORD64)item[CONFIG_MAXBYTESPERSAMPLE];
            }
            if (item[CONFIG_MAXTOTALBYTES])
            {
                aSpec.PerSampleLimits.dwlMaxTotalBytes = (DWORD64)item[CONFIG_MAXTOTALBYTES];
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
        CryptoHashStream::Algorithm algorithms = CryptoHashStream::Algorithm::Undefined;
        std::set<wstring> keys;
        boost::split(keys, (std::wstring_view)configitem[GETTHIS_HASH], boost::is_any_of(L","));

        for (const auto& key : keys)
        {
            const auto alg = CryptoHashStream::GetSupportedAlgorithm(key.c_str());
            if (alg == CryptoHashStream::Algorithm::Undefined)
            {
                Log::Warn(L"Hash algorithm '{}' is not supported", key);
            }
            else
            {
                algorithms |= alg;
            }
        }

        config.CryptoHashAlgs = algorithms;
    }

    if (configitem[GETTHIS_FUZZYHASH])
    {
        std::set<wstring> keys;
        boost::split(keys, (std::wstring_view)configitem[GETTHIS_FUZZYHASH], boost::is_any_of(L","));

        for (const auto& key : keys)
        {
            auto alg = FuzzyHashStream::GetSupportedAlgorithm(key.c_str());
            if (alg == FuzzyHashStream::Algorithm::Undefined)
            {
                Log::Warn(L"Fuzzy hash algorithm '{}' is not supported", key);
            }
            else
            {
                config.FuzzyHashAlgs |= alg;
            }
        }
    }
    if (configitem[GETTHIS_YARA])
    {
        auto yaraConfig = YaraConfig::Get(configitem[GETTHIS_YARA]);
        if (!yaraConfig)
        {
            Log::Error(L"Failed to parse Yara configuration [{}]", yaraConfig.error());
            return ToHRESULT(yaraConfig.error());
        }

        config.Yara = std::make_unique<YaraConfig>(*yaraConfig);
    }

    if (configitem[GETTHIS_RESURRECT])
    {
        auto rv = ToResurrectRecordsMode(configitem[GETTHIS_RESURRECT].c_str());
        if (!rv)

        {
            Log::Error(
                L"Failed to parse 'Resurrect' attribute (value: {}) [{}]", configitem[GETTHIS_RESURRECT], rv.error());
        }
        else
        {
            config.resurrectRecordsMode = *rv;
        }
    }

    return S_OK;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
    HRESULT hr = S_OK;
    std::wstring strContent;

    try
    {
        UtilitiesLoggerConfiguration::Parse(argc, argv, m_utilitiesConfig.log);

        for (int i = 0; i < argc; i++)
        {
            bool bSkipDeleted = false;
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
                    else if (ResurrectRecordsOption(argv[i] + 1, L"ResurrectRecords", config.resurrectRecordsMode))
                        ;
                    else if (!_wcsnicmp(argv[i] + 1, L"Sample", wcslen(L"Sample")))
                    {
                        LPCWSTR pEquals = wcschr(argv[i], L'=');
                        if (!pEquals)
                        {
                            Log::Error("Option /Sample should be like: /Sample=malware.exe");
                            return E_INVALIDARG;
                        }
                        else
                        {
                            std::wstring_view match(pEquals + 1);
                            if (match.size() > 1 && match[0] == L'\\')
                            {
                                auto spec = make_shared<FileFind::SearchTerm>();
                                spec->Required = FileFind::SearchTerm::Criteria::PATH_MATCH;
                                spec->Path = match;

                                SampleSpec aSpec;
                                aSpec.Terms.push_back(spec);
                                aSpec.Content.Type = ContentType::INVALID;
                                config.listofSpecs.push_back(aSpec);
                            }
                            else
                            {
                                SampleSpec aSpec;
                                auto filespec = make_shared<FileFind::SearchTerm>(std::wstring(pEquals + 1));
                                aSpec.Terms.push_back(filespec);
                                aSpec.Content.Type = ContentType::INVALID;
                                config.listofSpecs.push_back(aSpec);
                            }
                        }
                    }
                    else if (BooleanOption(argv[i] + 1, L"FlushRegistry", config.bFlushRegistry))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"ReportAll", config.bReportAll))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"NoLimits", config.limits.bIgnoreLimits))
                        ;
                    else if (ShadowsOption(argv[i] + 1, L"Shadows", config.bAddShadows, config.m_shadows))
                        ;
                    else if (LocationExcludeOption(argv[i] + 1, L"Exclude", config.m_excludes))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Password", config.Output.Password))
                        ;
                    else if (FileSizeOption(argv[i] + 1, L"MaxPerSampleBytes", config.limits.dwlMaxBytesPerSample))
                        ;
                    else if (FileSizeOption(argv[i] + 1, L"MaxTotalBytes", config.limits.dwlMaxTotalBytes))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"MaxSampleCount", config.limits.dwMaxSampleCount))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"NoLimits", config.limits.bIgnoreLimits))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Compression", config.Output.Compression))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Content", strContent))
                    {
                        config.content = config.GetContentSpecFromString(strContent);
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
                        Log::Error(L"Failed to parse command line item: '{}'", argv[i] + 1);
                        PrintUsage();
                        return E_INVALIDARG;
                    }
                    break;
                default:
                    break;
            }
        }

        LocationSet::ParseLocationsFromArgcArgv(argc, argv, config.inputLocations);

        if (FAILED(hr = config.Locations.AddLocationsFromArgcArgv(argc, argv)))
        {
            Log::Error("Error in specific locations parsing");
            return E_INVALIDARG;
        }
    }
    catch (...)
    {
        Log::Error("GetThis failed during argument parsing, exiting");
        return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    HRESULT hr = E_FAIL;

    UtilitiesLoggerConfiguration::Apply(m_logging, m_utilitiesConfig.log);

    config.Locations.SetShadowCopyParser(config.m_shadowsParser.value_or(Ntfs::ShadowCopy::ParserType::kInternal));

    ::InitializeStatisticsOutput(config);

    if (boost::logic::indeterminate(config.bAddShadows))
    {
        config.bAddShadows = false;
    }

    if (config.inputLocations.empty())
    {
        Log::Critical("Missing location parameter");
    }

    config.Locations.Consolidate(
        (bool)config.bAddShadows,
        config.m_shadows.value_or(LocationSet::ShadowFilters()),
        config.m_excludes.value_or(LocationSet::PathExcludes()),
        FSVBR::FSType::NTFS);

    if (config.Output.Type == OutputSpec::Kind::None || config.Output.Path.empty())
    {
        Log::Warn(L"No output explicitely specified: creating GetThis.7z in current directory");
        config.Output.Path = L"GetThis.7z";
        config.Output.Type = OutputSpec::Kind::Archive;
        config.Output.ArchiveFormat = ArchiveFormat::SevenZip;
    }

    if (config.Output.Type == OutputSpec::Kind::Archive && config.Output.Compression.empty())
    {
        config.Output.Compression = L"Normal";
    }

    if (config.content.Type == ContentType::INVALID)
    {
        config.content.Type = ContentType::DATA;
    }

    bool hasFailed = false;
    std::for_each(begin(config.listofSpecs), end(config.listofSpecs), [this, &hasFailed](SampleSpec& aSpec) {
        if (aSpec.Content.Type == ContentType::INVALID)
        {
            aSpec.Content = config.content;
        }

        std::for_each(
            begin(aSpec.Terms), end(aSpec.Terms), [this, &hasFailed](const shared_ptr<FileFind::SearchTerm>& filespec) {
                HRESULT hr = FileFinder.AddTerm(filespec);
                if (FAILED(hr))
                {
                    hasFailed = true;
                    Log::Error(
                        L"Failed to parse search term (item: {}) [{}]", filespec->YaraRulesSpec, SystemError(hr));
                }
            });
    });

    if (hasFailed)
    {
        Log::Error("Failed to parse search term");
        return E_FAIL;
    }

    std::for_each(
        begin(config.listOfExclusions),
        end(config.listOfExclusions),
        [this](const std::shared_ptr<FileFind::SearchTerm>& aTerm) { FileFinder.AddExcludeTerm(aTerm); });

    // TODO: make a function to use also in GetSamples_config.cpp
    if (!config.limits.bIgnoreLimits
        && (!config.limits.dwlMaxTotalBytes.has_value() && !config.limits.dwMaxSampleCount.has_value()))
    {
        Log::Critical(
            L"No global (at samples level, MaxTotalBytes or MaxSampleCount) has been set: set limits in configuration "
            L"or use /nolimits");
        return E_INVALIDARG;
    }

    GlobalLimits = config.limits;

    if (FAILED(hr = FileFinder.InitializeYara(config.Yara)))
    {
        Log::Error(L"Failed to initialize yara scanner");
        return hr;
    }

    if (FAILED(hr = FileFinder.CheckYara()))
        return hr;

    return S_OK;
}
