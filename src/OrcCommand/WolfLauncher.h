//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcCommand.h"

#include <memory>
#include <string>
#include <agents.h>
#include <set>
#include <chrono>

#include "TableOutput.h"

#include "CommandMessage.h"
#include "CommandAgent.h"

#include "UtilitiesMain.h"

#include "ConfigFile.h"

#include "WolfExecution.h"

#include <boost\logic\tribool.hpp>

#pragma managed(push, off)

using namespace std;

namespace Orc::Command::Wolf {

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

    enum WolfPowerState
    {
        Unmodified = 0L,
        SystemRequired = ES_SYSTEM_REQUIRED,
        DisplayRequired = ES_DISPLAY_REQUIRED,
        UserPresent = ES_USER_PRESENT,
        AwayMode = ES_AWAYMODE_REQUIRED
    };

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
        WolfExecution::Repeat RepeatBehavior = WolfExecution::NotSet;

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

        std::set<wstring> strPowerStates;

        WolfPowerState PowerState = WolfPowerState::Unmodified;
        LocationSet::Altitude DefaultAltitude = LocationSet::Altitude::NotSet;
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

    Main(logger pLog)
        : UtilitiesMain(std::move(pLog)) {};

    // defined in WolfLauncher_Output.cpp
    virtual void PrintHeader(LPCWSTR szToolName, LPCWSTR szDescription, LPCWSTR szVersion);
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
    Configuration config;

    std::vector<std::unique_ptr<WolfExecution>> m_wolfexecs;

    std::shared_ptr<ByteStream> m_pConfigStream;
    std::shared_ptr<ByteStream> m_pLocalConfigStream;

    std::shared_ptr<UploadAgent> m_pUploadAgent;
    std::unique_ptr<UploadMessage::UnboundedMessageBuffer> m_pUploadMessageQueue;
    std::unique_ptr<Concurrency::call<UploadNotification::Notification>> m_pUploadNotification;

    std::vector<std::shared_ptr<WolfExecution::Recipient>> m_Recipients;

    std::shared_ptr<WolfExecution::Recipient> GetRecipient(const std::wstring& strName)
    {
        auto it = std::find_if(
            begin(m_Recipients), end(m_Recipients), [&strName](const std::shared_ptr<WolfExecution::Recipient>& item) {
                return !strName.compare(item->Name);
            });
        if (it != end(m_Recipients))
            return *it;
        return nullptr;
    }

    HRESULT InitializeUpload(const OutputSpec::Upload& uploadspec);
    HRESULT UploadSingleFile(const std::wstring& fileName, const std::wstring& filePath);
    HRESULT CompleteUpload();

    std::shared_ptr<WolfExecution::Recipient> GetRecipientFromItem(const ConfigItem& item);

    boost::logic::tribool SetWERDontShowUI(DWORD dwNewValue);  // returns DontShowUI previous value

    HRESULT CreateAndUploadOutline();

    HRESULT SetLauncherPriority(WolfPriority priority);

    HRESULT SetDefaultAltitude()
    {
        LocationSet::ConfigureDefaultAltitude(_L_, config.DefaultAltitude);
        return S_OK;
    }

    HRESULT Run_Execute();
    HRESULT Run_Keywords();
};
}  // namespace Orc::Command::Wolf
#pragma managed(pop)
