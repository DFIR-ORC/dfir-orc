//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "UtilitiesMain.h"

#include <memory>
#include <string>
#include <agents.h>
#include <set>
#include <chrono>

#include <boost/logic/tribool.hpp>

#include "OrcCommand.h"
#include "CommandMessage.h"
#include "CommandAgent.h"
#include "ConfigFile.h"
#include "WolfExecution.h"
#include "TableOutput.h"

#include "Output/Console/Journal.h"
#include "Utils/EnumFlags.h"

#pragma managed(push, off)

using namespace std::literals;

namespace Orc {
namespace Command::Wolf {

struct FileInformations;

class ORCUTILS_API Main : public UtilitiesMain
{
public:
    enum WolfLauncherAction
    {
        Execute = 0,
        Keywords,
        Dump,
        FromDump
    };

    enum WolfPriority
    {
        Low,
        Normal,
        High
    };

    enum class WolfPowerState
    {
        Unmodified = 0L,
        SystemRequired = ES_SYSTEM_REQUIRED,
        DisplayRequired = ES_DISPLAY_REQUIRED,
        UserPresent = ES_USER_PRESENT,
        AwayMode = ES_AWAYMODE_REQUIRED
    };

    static std::wstring ToString(WolfPowerState value);

    enum WolfLogMode
    {
        CreateNew = 0,
        Append
    };

    class Configuration : public UtilitiesMain::Configuration
    {
    public:
        Configuration()
        {
            Output.supportedTypes = OutputSpec::Kind::Directory;
            JobStatistics.supportedTypes =
                static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::SQL);
            ProcessStatistics.supportedTypes =
                static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::SQL);
            Log.supportedTypes = OutputSpec::Kind::File;
            Outline.supportedTypes = OutputSpec::Kind::StructuredFile;
            TempWorkingDir.supportedTypes = OutputSpec::Kind::Directory;
        };

        WolfLauncherAction SelectedAction = Execute;

        OutputSpec Output;
        OutputSpec JobStatistics;
        OutputSpec ProcessStatistics;

        OutputSpec Log;
        OutputSpec Outline;

        std::wstring strCompressionLevel;

        OutputSpec TempWorkingDir;

        std::optional<std::wstring> strOfflineLocation;

        std::chrono::milliseconds msRefreshTimer = 1s;
        std::chrono::milliseconds msArchiveTimeOut = 10min;
        std::chrono::milliseconds msCommandTerminationTimeOut = 3h;

        std::wstring strDbgHelp;

        boost::tribool bChildDebug = boost::indeterminate;
        bool bRepeatOnce = false;
        bool bRepeatCreateNew = false;
        bool bRepeatOverwrite = false;
        WolfExecution::Repeat RepeatBehavior = WolfExecution::Repeat::NotSet;

        bool bBeepWhenDone = false;
        bool bAddConfigToArchive = true;
        bool bUseJournalWhenEncrypting = true;
        bool bNoJournaling = false;
        bool bTeeClearTextOutput = false;
        bool bWERDontShowUI = false;

        std::set<std::wstring, CaseInsensitive> EnableKeywords;
        std::set<std::wstring, CaseInsensitive> DisableKeywords;
        std::set<std::wstring, CaseInsensitive> OnlyTheseKeywords;

        WolfPriority Priority = WolfPriority::Low;

        std::set<std::wstring> strPowerStates;

        WolfPowerState PowerState = WolfPowerState::Unmodified;
        LocationSet::Altitude DefaultAltitude = LocationSet::Altitude::NotSet;
        std::vector<std::shared_ptr<WolfExecution::Recipient>> m_Recipients;
    };

public:
    static LPCWSTR ToolName() { return L"WolfLauncher"; }
    static LPCWSTR ToolDescription() { return L"DFIR-ORC command scheduler"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return L"res:#WOLFLAUNCHER_CONFIG"; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder();
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return L"xml"; }

    static LPCWSTR DefaultSchema() { return L"res:#WOLFLAUNCHER_SQLSCHEMA"; }

    Main()
        : UtilitiesMain()
        , m_journal(m_console) {};

    void PrintUsage();
    void PrintFooter();
    void PrintParameters();

    HRESULT GetSchemaFromConfig(const ConfigItem& schemaitem);
    HRESULT GetConfigurationFromConfig(const ConfigItem& configitem);
    HRESULT GetLocalConfigurationFromConfig(const ConfigItem& configitem);

    HRESULT GetConfigurationFromArgcArgv(int argc, const WCHAR* argv[]);

    HRESULT CheckConfiguration();

    HRESULT Run();

private:
    OrcCommand::Output::Journal m_journal;

    Configuration config;

    std::vector<std::unique_ptr<WolfExecution>> m_wolfexecs;

    std::shared_ptr<ByteStream> m_pConfigStream;
    std::shared_ptr<ByteStream> m_pLocalConfigStream;

    std::shared_ptr<UploadAgent> m_pUploadAgent;
    std::unique_ptr<UploadMessage::UnboundedMessageBuffer> m_pUploadMessageQueue;
    std::unique_ptr<Concurrency::call<UploadNotification::Notification>> m_pUploadNotification;

    std::shared_ptr<WolfExecution::Recipient> GetRecipient(const std::wstring& strName);

    HRESULT GetOutputFileInformations(const WolfExecution& exec, FileInformations& fileInformations);

    HRESULT InitializeUpload(const OutputSpec::Upload& uploadspec);
    HRESULT UploadSingleFile(const std::wstring& fileName, const std::wstring& filePath);
    HRESULT CompleteUpload();

    std::shared_ptr<WolfExecution::Recipient> GetRecipientFromItem(const ConfigItem& item);

    HRESULT SetWERDontShowUI(DWORD dwNewValue, DWORD& dwOldValue);

    HRESULT CreateAndUploadOutline();

    HRESULT SetLauncherPriority(WolfPriority priority);

    HRESULT SetDefaultAltitude()
    {
        LocationSet::ConfigureDefaultAltitude(config.DefaultAltitude);
        return S_OK;
    }

    HRESULT Run_Execute();
    HRESULT ExecuteKeyword(WolfExecution& execution);
    HRESULT Run_Keywords();
};

}  // namespace Command::Wolf

ENABLE_BITMASK_OPERATORS(Command::Wolf::Main::WolfPowerState);

}  // namespace Orc

#pragma managed(pop)
