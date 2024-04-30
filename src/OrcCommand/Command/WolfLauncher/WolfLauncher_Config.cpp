//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <filesystem>

#include <boost/algorithm/string.hpp>
#include <boost/tokenizer.hpp>

#include "WolfLauncher.h"

#include "Configuration/ConfigFileReader.h"
#include "Configuration/ConfigFileWriter.h"
#include "TemporaryStream.h"
#include "TableOutputWriter.h"
#include "LocationSet.h"
#include "CaseInsensitive.h"
#include "SystemDetails.h"
#include "ConfigFile_OrcConfig.h"
#include "ConfigFile_WOLFLauncher.h"
#include "Log/UtilitiesLoggerConfiguration.h"
#include "Command/WolfLauncher/ConfigFile_WOLFLauncher.h"
#include "Command/WolfLauncher/ConsoleConfiguration.h"
#include "Configuration/Option.h"
#include "Text/Hex.h"
#include "Utils/WinApi.h"

using namespace Orc;
using namespace Orc::Command::Wolf;

namespace fs = std::filesystem;

namespace {

constexpr std::wstring_view kOrcOffline(L"ORC_Offline");

// Very close to std::filesystem::create_directories but keep tracks or created directories
void CreateDirectories(std::filesystem::path path, std::vector<std::wstring>& newDirectories, std::error_code& ec)
{
    if (std::filesystem::exists(path))
    {
        return;
    }

    auto parent = path.parent_path();
    if (!parent.empty())
    {
        CreateDirectories(parent, newDirectories, ec);
        if (ec)
        {
            Log::Debug(L"Failed to create directory: {} [{}]", parent, ec);
            return;
        }
    }

    if (std::filesystem::create_directory(path, ec))
    {
        newDirectories.push_back(path);
    }
}

bool IgnoreOptions(LPCWSTR szArg)
{
    const auto kConsole = "console";

    std::wstring_view arg(szArg);
    auto pos = arg.find_first_of(L':');
    if (pos != arg.npos)
    {
        return boost::iequals(kConsole, arg.substr(0, pos));
    }
    else
    {
        return boost::iequals(kConsole, arg);
    }
}

bool ParseNoLimitsArgument(std::wstring_view input, Main::Configuration& configuration)
{
    const auto kNoLimits = L"nolimits";
    if (input == kNoLimits)
    {
        configuration.bNoLimits = true;
        return true;
    }

    std::vector<Option> options;
    if (!ParseSubArguments(input, kNoLimits, options))
    {
        return false;
    }

    if (options.size() == 0)
    {
        return false;
    }

    for (auto& option : options)
    {
        if (option.value)
        {
            // Do not accept: '/nolimits:GetEvt=foobar'
            return false;
        }

        configuration.NoLimitsKeywords.insert(std::move(option.key));
    }

    return true;
}

void ApplyOptionNoLimits(const Main::Configuration& configuration, Orc::CommandMessage& command)
{
    if (configuration.bNoLimits == false
        && configuration.NoLimitsKeywords.find(command.Keyword()) == std::cend(configuration.NoLimitsKeywords))
    {
        return;
    }

    if (command.GetParameters().size() > 1)
    {
        const auto& keyword = command.GetParameters()[1].Keyword;
        if (boost::iequals(keyword, L"getthis") || boost::iequals(keyword, L"getsamples"))
        {
            command.PushArgument(command.GetParameters().size(), L"/nolimits");
        }
    }
}

struct CaseInsensitiveComparator
{
    bool operator()(const std::wstring& lhs, const std::wstring& rhs) const
    {
        // complies with strict weak ordering comparator requirement
        return std::lexicographical_compare(
            std::cbegin(lhs), std::cend(lhs), std::cbegin(rhs), std::cend(rhs), [](wchar_t l, wchar_t r) {
                return std::toupper(l) < std::toupper(r);
            });
    }
};

}  // namespace

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return Orc::Config::Wolf::root;
}

ConfigItem::InitFunction Main::GetXmlLocalConfigBuilder()
{
    return Orc::Config::Wolf::Local::root;
}

std::shared_ptr<WolfExecution::Recipient> Main::GetRecipientFromItem(const ConfigItem& recipient_item)
{
    HRESULT hr = E_FAIL;

    auto retval = std::make_shared<WolfExecution::Recipient>();

    if (!recipient_item[WOLFLAUNCHER_RECIPIENT_NAME])
    {
        Log::Error("Recipient name attribute is mandatory");
        return nullptr;
    }

    retval->Name = recipient_item[WOLFLAUNCHER_RECIPIENT_NAME];
    auto inlist = GetRecipient(retval->Name);
    if (inlist != nullptr)
    {
        Log::Error(L"Only one recipient with name '{}' is allowed", retval->Name);
        return nullptr;
    }

    if (!recipient_item[WOLFLAUNCHER_RECIPIENT_ARCHIVE])
    {
        Log::Error("Recipient archive attribute is mandatory");
        return nullptr;
    }

    boost::split(
        retval->ArchiveSpec, (std::wstring_view)recipient_item[WOLFLAUNCHER_RECIPIENT_ARCHIVE], boost::is_any_of(",;"));

    DWORD cbOutput = 0L;
    DWORD dwFormatFlags = 0L;

    const std::wstring& cert = recipient_item;

    if (!CryptStringToBinaryW(
            cert.c_str(), (DWORD)cert.size(), CRYPT_STRING_ANY, NULL, &cbOutput, NULL, &dwFormatFlags))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed CryptStringToBinary while converting {} into a binary blob [{}]", cert, SystemError(hr));
        return nullptr;
    }

    CBinaryBuffer pCertBin;
    pCertBin.SetCount(cbOutput);
    if (!CryptStringToBinaryW(
            cert.c_str(), (DWORD)cert.size(), CRYPT_STRING_ANY, pCertBin.GetData(), &cbOutput, NULL, &dwFormatFlags))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"CryptStringToBinary failed to convert '{}' into a binary blob [{}]", cert, SystemError(hr));
        return nullptr;
    }

    std::swap(retval->Certificate, pCertBin);

    return retval;
}

void Main::ReadLogConfiguration(const ConfigItem& configItem, bool hasConsoleConfigItem)
{
    if (!configItem.empty())
    {
        //
        // DEPRECATED: compatibility with 10.0.x log configuration
        //
        // Usually configuration is close to '<log ...>ORC_{SystemType}_{FullComputerName}_{TimeStamp}.log</log>'
        // instead of '<log><file><output>...</output></file></log>'.
        //
        // Silently convert any old configuration into new structures.
        //

        OutputSpec output;
        auto hr = output.Configure(configItem);
        if (FAILED(hr))
        {
            Log::Warn(L"Failed to configure DFIR-Orc log file [{}]", SystemError(hr));
        }
        else if (!hasConsoleConfigItem)
        {
            m_consoleConfiguration.output.path = output.Path;
            m_consoleConfiguration.output.encoding = ToEncoding(output.OutputEncoding);
            m_consoleConfiguration.output.disposition = ToFileDisposition(output.disposition);

            if (!m_utilitiesConfig.log.level)
            {
                m_utilitiesConfig.log.level = Log::Level::Info;
                UtilitiesLoggerConfiguration::ApplyLogLevel(m_logging, m_utilitiesConfig.log);
            }

            Log::Warn(
                L"This use of the configuration element '<log>' is DEPRECATED, you should use '<console> to capture "
                L"console output or keep using 'log' for detailed logging.");
        }
    }
    else if (configItem)
    {
        UtilitiesLoggerConfiguration::Parse(configItem, m_utilitiesConfig.log);
    }
}

HRESULT Main::GetConfigurationFromConfig(const ConfigItem& configitem)
{
    using namespace std::string_view_literals;

    HRESULT hr = E_FAIL;

    ConfigFile reader;

    if (config.bAddConfigToArchive)
    {
        auto stream = std::make_shared<TemporaryStream>();

        hr = stream->Open(config.TempWorkingDir.Path, L"WolfConfigStream", 100 * 1024);
        if (SUCCEEDED(hr))
        {
            ConfigFileWriter w;

            hr = w.WriteConfig(stream, L"DFIR-ORC Configuration", configitem);
            if (SUCCEEDED(hr))
            {
                m_pConfigStream = stream;
            }
            else
            {
                Log::Error(L"Failed to write configuration [{}]", SystemError(hr));
            }
        }
    }

    config.bChildDebug = boost::indeterminate;
    if (configitem[WOLFLAUNCHER_CHILDDEBUG])
    {
        if (equalCaseInsensitive(L"No"sv, (const std::wstring&)configitem[WOLFLAUNCHER_CHILDDEBUG]))
        {
            config.bChildDebug = false;
        }
        else
        {
            config.bChildDebug = true;
        }
    }

    if (configitem[WOLFLAUNCHER_WERDONTSHOWUI])
    {
        if (equalCaseInsensitive(L"No"sv, (const std::wstring&)configitem[WOLFLAUNCHER_WERDONTSHOWUI]))
        {
            config.bWERDontShowUI = false;
        }
        else
        {
            config.bWERDontShowUI = true;
        }
    }

    ReadLogConfiguration(configitem[WOLFLAUNCHER_LOG], configitem[WOLFLAUNCHER_CONSOLE].Status == ConfigItem::PRESENT);

    if (configitem[WOLFLAUNCHER_CONSOLE])
    {
        ConsoleConfiguration::Parse(configitem[WOLFLAUNCHER_CONSOLE], m_consoleConfiguration);
    }

    if (configitem[WOLFLAUNCHER_OUTLINE])
    {
        auto hr = E_FAIL;
        if (FAILED(hr = config.Outline.Configure(configitem[WOLFLAUNCHER_OUTLINE])))
        {
            Log::Warn(L"Failed to configure DFIR-Orc outline file [{}]", SystemError(hr));
        }
    }

    if (configitem[WOLFLAUNCHER_OUTCOME])
    {
        auto hr = E_FAIL;
        if (FAILED(hr = config.Outcome.Configure(configitem[WOLFLAUNCHER_OUTCOME])))
        {
            Log::Warn(L"Failed to configure DFIR-Orc outcome file [{}]", SystemError(hr));
        }
    }

    if (!configitem[WOLFLAUNCHER_GLOBAL_CMD_TIMEOUT])
    {
        config.msCommandTerminationTimeOut = 3h;
    }
    else
    {
        DWORD dwCommandTimeOutInMinutes = 0L;
        hr = GetIntegerFromArg(configitem[WOLFLAUNCHER_GLOBAL_CMD_TIMEOUT].c_str(), dwCommandTimeOutInMinutes);
        if (FAILED(hr))
        {
            Log::Error(
                L"Invalid command timeout (command_timeout) value specified '{}' must be an integer [{}]",
                configitem[WOLFLAUNCHER_GLOBAL_CMD_TIMEOUT].c_str(),
                SystemError(hr));
            return hr;
        }

        config.msCommandTerminationTimeOut = std::chrono::minutes(dwCommandTimeOutInMinutes);
    }

    if (!configitem[WOLFLAUNCHER_GLOBAL_ARCHIVE_TIMEOUT])
    {
        config.msArchiveTimeOut = 5min;
    }
    else
    {
        DWORD dwArchiveTimeOutMinutes = 0L;
        hr = GetIntegerFromArg(configitem[WOLFLAUNCHER_GLOBAL_ARCHIVE_TIMEOUT].c_str(), dwArchiveTimeOutMinutes);
        if (FAILED(hr))
        {
            Log::Error(
                L"Invalid archive timeout (archive_timeout) specified '{}' must be an integer [{}]",
                configitem[WOLFLAUNCHER_GLOBAL_ARCHIVE_TIMEOUT].c_str(),
                SystemError(hr));
            return hr;
        }

        config.msArchiveTimeOut = std::chrono::minutes(dwArchiveTimeOutMinutes);
    }

    if (configitem[WOLFLAUNCHER_RECIPIENT])
    {
        for (auto& recipient_item : configitem[WOLFLAUNCHER_RECIPIENT].NodeList)
        {
            auto recipient = GetRecipientFromItem(recipient_item);
            if (recipient != nullptr)
            {
                config.m_Recipients.push_back(recipient);
            }
        }
    }

    if (configitem[WOLFLAUNCHER_UPLOAD])
    {
        auto upload = std::make_shared<OutputSpec::Upload>();

        if (auto hr = upload->Configure(configitem[WOLFLAUNCHER_UPLOAD]); FAILED(hr))
        {
            Log::Error("Error in specified upload section in config file, ignored [{}]", SystemError(hr));
            return hr;
        }

        config.Output.UploadOutput = upload;
    }

    for (const ConfigItem& archiveitem : configitem[WOLFLAUNCHER_ARCHIVE].NodeList)
    {
        auto exec = std::make_unique<WolfExecution>(m_journal, m_outcome);
        hr = exec->SetJobConfigFromConfig(archiveitem);
        if (FAILED(hr))
        {
            Log::Error("Job configuration failed [{}]", SystemError(hr));
            return hr;
        }

        hr = exec->SetJobTimeOutFromConfig(archiveitem, config.msCommandTerminationTimeOut, config.msArchiveTimeOut);
        if (FAILED(hr))
        {
            Log::Error(L"Job timeout configuration failed [{}]", SystemError(hr));
            return hr;
        }

        hr = exec->SetRestrictionsFromConfig(archiveitem.SubItems[WOLFLAUNCHER_ARCHIVE_RESTRICTIONS]);
        if (FAILED(hr))
        {
            Log::Error(L"Command restrictions setup failed [{}]", SystemError(hr));
            return hr;
        }

        hr = exec->SetRepeatBehaviourFromConfig(archiveitem.SubItems[WOLFLAUNCHER_ARCHIVE_REPEAT]);
        if (FAILED(hr))
        {
            Log::Error(L"Command repeat behavior setup failed [{}]", SystemError(hr));
            return hr;
        }

        hr = exec->SetCommandsFromConfig(archiveitem.SubItems[WOLFLAUNCHER_ARCHIVE_COMMAND]);
        if (FAILED(hr))
        {
            Log::Error(L"Command creation failed [{}]", SystemError(hr));
            return hr;
        }

        hr = exec->SetArchiveName((const std::wstring&)archiveitem[WOLFLAUNCHER_ARCHIVE_NAME]);
        if (FAILED(hr))
        {
            Log::Error(
                L"Failed to set '{}' as archive name [{}]", archiveitem[WOLFLAUNCHER_ARCHIVE_NAME], SystemError(hr));
            return hr;
        }

        hr = exec->SetConfigStreams(m_pConfigStream, m_pLocalConfigStream);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to set config stream [{}]", archiveitem[WOLFLAUNCHER_ARCHIVE_NAME], SystemError(hr));
            return hr;
        }

        if (archiveitem[WOLFLAUNCHER_ARCHIVE_OPTIONAL])
        {
            if (!_wcsicmp(archiveitem[WOLFLAUNCHER_ARCHIVE_OPTIONAL].c_str(), L"no"))
            {
                exec->SetMandatory();
            }
            else
            {
                exec->SetOptional();
            }
        }

        if (archiveitem[WOLFLAUNCHER_ARCHIVE_CHILDDEBUG])
        {
            if (!_wcsicmp(archiveitem[WOLFLAUNCHER_ARCHIVE_CHILDDEBUG].c_str(), L"no"))
            {
                exec->UnSetChildDebug();
            }
            else
            {
                exec->SetChildDebug();
            }
        }

        m_wolfexecs.push_back(std::move(exec));
    }

    return S_OK;
}

HRESULT Main::GetLocalConfigurationFromConfig(const ConfigItem& configitem)
{
    std::wstring strOutputDir;

    if (config.bAddConfigToArchive && configitem)
    {
        auto stream = std::make_shared<TemporaryStream>();

        if (auto hr = stream->Open(config.TempWorkingDir.Path, L"DFIROrcConfigStream", 100 * 1024); FAILED(hr))
            return hr;

        ConfigFileWriter w;

        if (auto hr = w.WriteConfig(stream, L"DFIR-ORC Local Configuration", configitem); FAILED(hr))
            return hr;

        m_pLocalConfigStream = stream;
    }

    if (auto hr = config.Output.Configure(OutputSpec::Kind::Directory, configitem[ORC_OUTPUT]); FAILED(hr))
    {
        Log::Error("Error in specified outputdir in config file, using default [{}]", SystemError(hr));
        return hr;
    }

    if (configitem[ORC_UPLOAD])
    {
        auto upload = std::make_shared<OutputSpec::Upload>();

        if (upload == nullptr)
            return E_OUTOFMEMORY;

        if (auto hr = upload->Configure(configitem[ORC_UPLOAD]); FAILED(hr))
        {
            Log::Error("Error in specified upload section in config file, ignored [{}]", SystemError(hr));
            return hr;
        }
        config.Output.UploadOutput = upload;
    }

    if (auto hr = config.TempWorkingDir.Configure(OutputSpec::Kind::Directory, configitem[ORC_TEMP]); FAILED(hr))
    {
        Log::Error("Error in specified tempdir in config file, defaulting to current directory [{}]", SystemError(hr));
        config.TempWorkingDir.Path = L".\\DFIR-OrcTempDir";
        config.TempWorkingDir.Type = OutputSpec::Kind::Directory;
    }

    if (configitem[ORC_RECIPIENT])
    {
        for (auto& recipient_item : configitem[ORC_RECIPIENT].NodeList)
        {
            auto recipient = GetRecipientFromItem(recipient_item);
            if (recipient == nullptr)
            {
                return E_FAIL;
            }

            if (recipient == nullptr)
                return E_FAIL;

            config.m_Recipients.push_back(recipient);
        }
    }

    if (configitem[ORC_PRIORITY])
    {
        if (!_wcsicmp(L"Normal", configitem[ORC_PRIORITY].c_str()))
            config.Priority = WolfPriority::Normal;
        else if (!_wcsicmp(L"Low", configitem[ORC_PRIORITY].c_str()))
            config.Priority = WolfPriority::Low;
        else if (!_wcsicmp(L"High", configitem[ORC_PRIORITY].c_str()))
            config.Priority = WolfPriority::High;
    }

    if (configitem[ORC_POWERSTATE])
    {
        std::set<std::wstring> powerstates;

        CSVListToContainer(configitem[ORC_POWERSTATE], config.strPowerStates);
    }

    if (configitem[ORC_ALTITUDE])
    {
        config.DefaultAltitude = LocationSet::GetAltitudeFromString(configitem[ORC_ALTITUDE]);
    }

    if (configitem[ORC_KEY])
    {
        for (const auto& item : configitem[ORC_KEY].NodeList)
        {
            CSVListToContainer(item, config.OnlyTheseKeywords);
        }
    }

    if (configitem[ORC_ENABLE_KEY])
    {
        for (const auto& item : configitem[ORC_ENABLE_KEY].NodeList)
        {
            CSVListToContainer(item, config.EnableKeywords);
        }
    }

    if (configitem[ORC_DISABLE_KEY])
    {
        for (const auto& item : configitem[ORC_DISABLE_KEY].NodeList)
        {
            CSVListToContainer(item, config.DisableKeywords);
        }
    }

    if (configitem[ORC_LOGGING])
    {
        ReadLogConfiguration(configitem[ORC_LOGGING], configitem[ORC_CONSOLE].Status == ConfigItem::PRESENT);
    }

    if (configitem[ORC_CONSOLE])
    {
        ConsoleConfiguration::Parse(configitem[ORC_CONSOLE], m_consoleConfiguration);
    }

    return S_OK;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
    HRESULT hr = E_FAIL;

    try
    {
        bool bExecute = false;
        bool bKeywords = false;
        bool bDump = false;
        bool bFromDump = false;

        std::wstring strTags;

        ConsoleConfiguration::Parse(argc, argv, m_consoleConfiguration);

        UtilitiesLoggerConfiguration logConfiguration;
        UtilitiesLoggerConfiguration::Parse(argc, argv, m_utilitiesConfig.log);

        // The cli log level option ('/debug', ...) supersede the console sink's level
        UtilitiesLoggerConfiguration::Parse(argc, argv, logConfiguration);
        if (logConfiguration.level)
        {
            m_utilitiesConfig.log.console.level.reset();
        }

        for (int i = 0; i < argc; i++)
        {
            std::wstring strPriority;
            std::wstring_view arg(argv[i] + 1);

            switch (argv[i][0])
            {
                case L'/':
                case L'-':
                    if (::ParseNoLimitsArgument(arg, config))
                        ;
                    else if (OutputOption(argv[i] + 1, L"Out", OutputSpec::Kind::Directory, config.Output))
                        ;
                    else if (OutputOption(argv[i] + 1, L"TempDir", OutputSpec::Kind::Directory, config.TempWorkingDir))
                        ;
                    else if (OutputOption(argv[i] + 1, L"Outline", OutputSpec::Kind::StructuredFile, config.Outline))
                        ;
                    else if (OutputOption(argv[i] + 1, L"Outcome", OutputSpec::Kind::StructuredFile, config.Outcome))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"Keys", bKeywords))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"Execute", bExecute))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"Dump", bDump))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"FromDump", bFromDump))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"NoChildDebug", config.bChildDebug))
                        config.bChildDebug = !config.bChildDebug;
                    else if (BooleanOption(argv[i] + 1, L"ChildDebug", config.bChildDebug))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"Once", config.bRepeatOnce))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"Overwrite", config.bRepeatOverwrite))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"CreateNew", config.bRepeatCreateNew))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Compression", config.strCompressionLevel))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Mothership", config.strMothershipHandle))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"archive_timeout", config.msArchiveTimeOut))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"command_timeout", config.msCommandTerminationTimeOut))
                        ;
                    else if (ParameterListOption(argv[i] + 1, L"key-", config.DisableKeywords, L","))
                        ;
                    else if (ParameterListOption(argv[i] + 1, L"-key", config.DisableKeywords, L","))
                        ;
                    else if (ParameterListOption(argv[i] + 1, L"key+", config.EnableKeywords, L","))
                        ;
                    else if (ParameterListOption(argv[i] + 1, L"+key", config.EnableKeywords, L","))
                        ;
                    else if (ParameterListOption(argv[i] + 1, L"key", config.OnlyTheseKeywords, L","))
                        ;
                    else if (ParameterListOption(argv[i] + 1, L"power", config.strPowerStates, L","))
                        ;
                    else if (AltitudeOption(argv[i] + 1, L"Altitude", config.DefaultAltitude))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Tags", strTags))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Offline", config.strOfflineLocation))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"Beep", config.bBeepWhenDone))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Priority", strPriority))
                    {
                        if (!_wcsicmp(L"Normal", strPriority.c_str()))
                            config.Priority = WolfPriority::Normal;
                        else if (!_wcsicmp(L"Low", strPriority.c_str()))
                            config.Priority = WolfPriority::Low;
                        else if (!_wcsicmp(L"High", strPriority.c_str()))
                            config.Priority = WolfPriority::High;
                    }
                    else if (BooleanOption(argv[i] + 1, L"WERDontShowUI", config.bWERDontShowUI))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"tee_cleartext", config.bTeeClearTextOutput))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"no_journaling", config.bNoJournaling))
                    {
                        config.bUseJournalWhenEncrypting = false;
                    }
                    else if (EncodingOption(argv[i] + 1, config.Output.OutputEncoding))
                    {
                        config.TempWorkingDir.OutputEncoding = config.Output.OutputEncoding;
                    }
                    else if (WaitForDebuggerOption(argv[i] + 1))
                        ;
                    else if (UsageOption(argv[i] + 1))
                        ;
                    else if (IgnoreCommonOptions(argv[i] + 1))
                        ;
                    else if (::IgnoreOptions(argv[i] + 1))
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

        if (!strTags.empty())
        {
            using tokenizer =
                boost::tokenizer<boost::char_separator<wchar_t>, std::wstring::const_iterator, std::wstring>;

            tokenizer tokens(strTags, boost::char_separator<wchar_t>(L","));

            SystemTags tags;
            for (const auto& tag : tokens)
            {
                tags.insert(tag);
            }
            SystemDetails::SetSystemTags(std::move(tags));
        }

        config.SelectedAction = WolfLauncherAction::Execute;
        if (bExecute)
            config.SelectedAction = WolfLauncherAction::Execute;
        else if (bKeywords)
            config.SelectedAction = WolfLauncherAction::Keywords;
        else if (bDump)
            config.SelectedAction = WolfLauncherAction::Dump;
        else if (bFromDump)
            config.SelectedAction = WolfLauncherAction::FromDump;
    }
    catch (...)
    {
        Log::Info("WolfLauncher failed during argument parsing, exiting");
        return E_FAIL;
    }
    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    HRESULT hr = E_FAIL;

    GUID guid;
    hr = CoCreateGuid(&guid);
    if (FAILED(hr))
    {
        Log::Error("Failed to initialize execution guid [{}]", SystemError(hr));
        SecureZeroMemory(&guid, sizeof(guid));
        hr = S_OK;  // keep going
    }

    SystemDetails::SetOrcRunId(guid);
    Log::Debug(L"Starting DFIR-Orc (run id: {})", ToStringW(guid));

    if (m_consoleConfiguration.output.path)
    {
        m_consoleConfiguration.output.path =
            fs::path(config.Output.Path) / fs::path(*m_consoleConfiguration.output.path).filename();
        ConsoleConfiguration::Apply(m_standardOutput, m_consoleConfiguration);
    }

    fs::path logPath;
    if (m_utilitiesConfig.log.file.path)
    {
        // Apply the output directory path to the log file
        logPath = fs::path(config.Output.Path) / fs::path(*m_utilitiesConfig.log.file.path).filename();
        m_utilitiesConfig.log.file.path = logPath;
    }

    if (m_utilitiesConfig.log.logFile)
    {
        // Deprecated: 10.0.x compatibility options
        // Apply the output directory path to the log file
        logPath = fs::path(config.Output.Path) / fs::path(*m_utilitiesConfig.log.logFile).filename();
        m_utilitiesConfig.log.logFile = logPath;
    }

    if (!config.strMothershipHandle.empty())
    {
        auto handle = Text::FromHexToLittleEndian<HANDLE>(std::wstring_view(config.strMothershipHandle));
        if (handle)
        {
            m_hMothership = *handle;
        }
        else
        {
            Log::Error("Failed to parse mothership handle [{}]", handle.error());
        }
    }
    else
    {
        Log::Warn("Missing mothership handle");
    }

    UtilitiesLoggerConfiguration::Apply(m_logging, m_utilitiesConfig.log);

    if ((config.bRepeatCreateNew ? 1 : 0) + (config.bRepeatOnce ? 1 : 0) + (config.bRepeatOverwrite ? 1 : 0) > 1)
    {
        Log::Error("options /createnew, /once and /overwrite are mutually exclusive");
        return E_FAIL;
    }

    if (config.Output.Type == OutputSpec::Kind::None)
    {
        config.Output.OutputEncoding = OutputSpec::Encoding::UTF8;
        config.Output.Type = OutputSpec::Kind::Directory;

        std::error_code ec;
        config.Output.Path = GetWorkingDirectoryApi(ec);
        if (ec)
        {
            Log::Error("Failed GetWorkingDirectoryApi [{}]", ec);
            ec.clear();

            config.Output.Path = L".";
        }
    }

    if (config.Output.Type == OutputSpec::Kind::Directory)
    {
        std::error_code ec;
        config.Output.Path = GetFullPathNameApi(config.Output.Path, ec);
        if (ec)
        {
            Log::Error("Failed GetFullPathNameApi: cannot resolve output directory [{}]", ec);
            ec.clear();
        }
    }

    if (config.Output.Type != OutputSpec::Kind::Directory)
    {
        Log::Error("Invalid output specification");
        return E_INVALIDARG;
    }

    if (config.TempWorkingDir.Type == OutputSpec::Kind::None)
    {
        const std::wstring workingTemp(L"%TEMP%\\WorkingTemp");

        // Prevent 'Configure' to create directories so they can be tracked for removal
        {
            std::error_code ec;

            auto temp = OutputSpec::Resolve(workingTemp);
            ::CreateDirectories(temp, m_emptyDirectoriesToRemove, ec);
            if (ec)
            {
                Log::Error(
                    L"Failed to pre-create working directory (initial value: {}, value: {}) [{}]",
                    workingTemp,
                    temp,
                    ec);
                ec.clear();
            }
        }

        if (FAILED(hr = config.TempWorkingDir.Configure(OutputSpec::Kind::Directory, workingTemp)))
        {
            Log::Error(L"Failed to use default temporary directory from %TEMP%");
            return hr;
        }
    }
    if (config.TempWorkingDir.Type != OutputSpec::Kind::Directory)
    {
        Log::Error("Invalid temporary location specification");
        return E_INVALIDARG;
    }

    if (config.Outline.Type != OutputSpec::Kind::None)
    {
        if (config.Outline.IsStructuredFile())
        {
            // We need to apply the output directory path to the outline file
            config.Outline.Path = std::filesystem::path(config.Output.Path) / config.Outline.FileName;
        }
        else
        {
            Log::Error(L"Invalid outline file type");
            return E_INVALIDARG;
        }
    }

    if (config.Outcome.Type != OutputSpec::Kind::None)
    {
        if (config.Outcome.IsStructuredFile())
        {
            // We need to apply the output directory path to the outline file
            config.Outcome.Path = std::filesystem::path(config.Output.Path) / config.Outcome.FileName;
        }
        else
        {
            Log::Error(L"Invalid outline file type");
            return E_INVALIDARG;
        }
    }

    if (config.bRepeatCreateNew)
    {
        config.RepeatBehavior = WolfExecution::Repeat::CreateNew;
    }

    if (config.bRepeatOnce)
    {
        config.RepeatBehavior = WolfExecution::Repeat::Once;
    }

    if (config.bRepeatOverwrite)
    {
        config.RepeatBehavior = WolfExecution::Repeat::Overwrite;
    }

    for (const auto& wolfexec : m_wolfexecs)
    {
        wolfexec->SetRepeatBehaviour(config.RepeatBehavior);
        wolfexec->SetOutput(config.Output, config.TempWorkingDir);
        wolfexec->SetRecipients(config.m_Recipients);

        if (!config.strCompressionLevel.empty())
        {
            Log::Debug(
                L"Command has no compression level, using main compression level: {}", config.strCompressionLevel);
            wolfexec->SetCompressionLevel(config.strCompressionLevel);
        }

        HRESULT hr = wolfexec->BuildFullArchiveName();
        if (FAILED(hr))
        {
            Log::Error(L"Failed to build full archive name for '{}' [{}]", wolfexec->GetKeyword(), SystemError(hr));
        }
    }

    for (const auto& powerState : config.strPowerStates)
    {
        if (!_wcsicmp(L"SystemRequired", powerState.c_str()))
        {
            config.PowerState = static_cast<WolfPowerState>(config.PowerState | WolfPowerState::SystemRequired);
        }
        else if (!_wcsicmp(L"DisplayRequired", powerState.c_str()))
        {
            config.PowerState = static_cast<WolfPowerState>(config.PowerState | WolfPowerState::DisplayRequired);
        }
        else if (!_wcsicmp(L"UserPresent", powerState.c_str()))
        {
            config.PowerState = static_cast<WolfPowerState>(config.PowerState | WolfPowerState::UserPresent);
        }
        else if (!_wcsicmp(L"AwayMode", powerState.c_str()))
        {
            config.PowerState = static_cast<WolfPowerState>(config.PowerState | WolfPowerState::AwayMode);
        }
        else
        {
            Log::Warn(L"Invalid powerstate value '{}': ignored", powerState);
        }
    }

    DWORD dwMajor = 0L, dwMinor = 0L;
    SystemDetails::GetOSVersion(dwMajor, dwMinor);

    if (dwMajor < 6)
    {
        config.bChildDebug = false;  // On XP (prior to Vista), using a dbghelp debugger fails
    }

    if (config.strOfflineLocation.has_value())
    {
        // DFIR-Orc is now in Offline mode (to handle disk images)

        // First, we check that dfir-orc computer & system type have been set
        std::wstring strFullComputerName;
        SystemDetails::GetFullComputerName(strFullComputerName);
        std::wstring strOrcFullComputerName;
        SystemDetails::GetOrcFullComputerName(strOrcFullComputerName);

        if (strFullComputerName == strOrcFullComputerName)
        {
            Log::Warn("DFIR-Orc in offline mode and no computer name set, defaulting to this machine's name");
        }

        // Then, we set ORC_Offline as a "OnlyThis" keyword
        if (config.OnlyTheseKeywords.empty())
        {
            config.OnlyTheseKeywords.insert(std::wstring(kOrcOffline));
        }

        // Finally, we set the %OfflineLocation env var so the location is known to chikdren
        SetEnvironmentVariable(L"OfflineLocation", config.strOfflineLocation.value().c_str());
    }

    std::map<std::wstring, bool, CaseInsensitiveComparator> processedKeys;
    for (const auto& key : config.OnlyTheseKeywords)
    {
        processedKeys[key] = false;
    }

    if (!config.OnlyTheseKeywords.empty())
    {
        for (const auto& exec : m_wolfexecs)
        {
            const auto& found = config.OnlyTheseKeywords.find(exec->GetKeyword());

            if (found != end(config.OnlyTheseKeywords))
            {
                processedKeys[exec->GetKeyword()] = true;
                exec->SetMandatory();
            }
            else
            {
                exec->SetOptional();
                for (const auto& command : exec->GetCommands())
                {
                    const auto& found = config.OnlyTheseKeywords.find(command->Keyword());

                    if (found != end(config.OnlyTheseKeywords))
                    {
                        exec->SetMandatory();
                        command->SetMandatory();
                        processedKeys[command->Keyword()] = true;
                    }
                    else
                    {
                        command->SetOptional();
                    }
                }
            }
        }
    }

    for (const auto& [key, status] : processedKeys)
    {
        if (status == false)
        {
            Log::Critical(L"Key not found: {}", key);
        }
    }

    if (!config.EnableKeywords.empty())
    {
        for (const auto& exec : m_wolfexecs)
        {
            const auto& found = config.EnableKeywords.find(exec->GetKeyword());

            if (found != end(config.EnableKeywords))
            {
                exec->SetMandatory();
            }
            else
            {
                for (const auto& command : exec->GetCommands())
                {
                    const auto& found = config.EnableKeywords.find(command->Keyword());

                    if (found != end(config.EnableKeywords))
                    {
                        exec->SetMandatory();
                        command->SetMandatory();
                    }
                }
            }
        }
    }

    if (!config.DisableKeywords.empty())
    {
        for (const auto& exec : m_wolfexecs)
        {
            const auto& found = config.DisableKeywords.find(exec->GetKeyword());

            if (found != end(config.DisableKeywords))
            {
                exec->SetOptional();
                for (const auto& command : exec->GetCommands())
                {
                    command->SetOptional();
                }
            }
            else
            {
                for (const auto& command : exec->GetCommands())
                {
                    const auto& found = config.DisableKeywords.find(command->Keyword());

                    if (found != end(config.DisableKeywords))
                    {
                        command->SetOptional();
                    }
                }
            }
        }
    }

    if (config.strOfflineLocation.has_value())
    {
        for (const auto& exec : m_wolfexecs)
        {
            for (const auto& command : exec->GetCommands())
            {
                if (!command->IsOptional() && !boost::iequals(exec->GetKeyword(), kOrcOffline))
                {
                    command->SetOptional();
                    m_journal.Print(exec->GetKeyword(), command->Keyword(), L"Error: incompatible with 'offline' mode");
                }
            }
        }
    }

    // Forward some options to executed commands like 'nolimits'
    for (const auto& exec : m_wolfexecs)
    {
        for (const auto& command : exec->GetCommands())
        {
            assert(command);
            ::ApplyOptionNoLimits(config, *command);
        }
    }

    return S_OK;
}
