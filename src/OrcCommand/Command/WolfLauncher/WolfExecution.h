//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <agents.h>

#include <regex>
#include <chrono>

#include <boost/logic/tribool.hpp>

#include "ArchiveMessage.h"
#include "ArchiveAgent.h"
#include "CommandAgent.h"
#include "CommandMessage.h"
#include "Configuration/ConfigFile.h"
#include "TableOutputWriter.h"
#include "UploadAgent.h"
#include "UploadMessage.h"
#include "Robustness.h"

#include "Command/WolfLauncher/Journal.h"
#include "Command/WolfLauncher/Outcome.h"

#pragma managed(push, off)

namespace Orc {

namespace Command::Wolf {

class WolfTask;

class WolfExecution
{
private:
    class WOLFExecutionTerminate : public TerminationHandler
    {
    private:
        WolfExecution* m_pExec;

    public:
        WOLFExecutionTerminate(const std::wstring& strKeyword, WolfExecution* pExec)
            : TerminationHandler(strKeyword, ROBUSTNESS_ARCHIVE)
            , m_pExec(pExec) {};
        HRESULT operator()()
        {

            if (m_pExec != nullptr)
            {
                return m_pExec->CompleteArchive(nullptr);
            }

            return S_OK;
        };
    };

public:
    enum class Repeat
    {
        NotSet = 0,
        CreateNew,
        Overwrite,
        Once
    };

    static std::wstring ToString(Repeat value);

    class Recipient
    {
    public:
        std::wstring Name;
        std::vector<std::wstring> ArchiveSpec;
        CBinaryBuffer Certificate;
    };

    std::chrono::milliseconds CmdTimeOut() const { return m_CmdTimeOut; }
    std::chrono::milliseconds ArchiveTimeOut() const { return m_ArchiveTimeOut; }

private:
    void WolfExecution::ArchiveNotificationHandler(const ArchiveNotification::Notification& notfication);
    CommandMessage::Message SetCommandFromConfigItem(const ConfigItem& item);
    HRESULT
    GetExecutableToRun(const ConfigItem& item, std::wstring& strExeToRun, std::wstring& strArgToAdd, bool& isSelf);
    HRESULT NotifyTask(const CommandNotification::Ptr& item);

private:
    Command::Wolf::Journal& m_journal;
    Command::Wolf::Outcome::Outcome& m_outcome;

    std::wstring m_commandSet;
    std::wstring m_strCompressionLevel;
    DWORD m_dwConcurrency;

    std::chrono::milliseconds m_CmdTimeOut;
    std::chrono::milliseconds m_ArchiveTimeOut;

    bool m_bUseJournalWhenEncrypting = true;
    bool m_bTeeClearTextOutput = false;

    bool m_bOptional = false;

    boost::tribool m_bChildDebug = boost::indeterminate;

    std::shared_ptr<WOLFExecutionTerminate> m_pTermination = nullptr;

    ArchiveMessage::UnboundedMessageBuffer m_ArchiveMessageBuffer;
    std::unique_ptr<Concurrency::call<ArchiveNotification::Notification>> m_archiveNotification;
    std::unique_ptr<ArchiveAgent> m_archiveAgent;
    std::wstring m_strArchiveName;
    std::shared_ptr<CryptoHashStream> m_archiveHashStream;
    OutputSpec m_Output;

    std::wstring m_strOutputFullPath;
    std::wstring m_strOutputFileName;
    std::wstring m_strArchiveFullPath;
    std::wstring m_strArchiveFileName;
    Repeat m_RepeatBehavior = Repeat::NotSet;

    OutputSpec m_Temporary;

    FILETIME m_StartTime;
    FILETIME m_FinishTime;
    FILETIME m_ArchiveFinishTime;

    std::vector<CommandMessage::Message> m_Commands;

    std::map<std::wstring, std::shared_ptr<WolfTask>> m_TasksByKeyword;
    DWORD m_dwLongerTaskKeyword = 0L;

    CommandMessage::PriorityMessageBuffer m_cmdAgentBuffer;
    std::unique_ptr<Concurrency::call<CommandNotification::Ptr>> m_cmdNotification;
    std::unique_ptr<CommandAgent> m_cmdAgent;

    JobRestrictions m_Restrictions;
    std::chrono::milliseconds m_ElapsedTime;

    std::unique_ptr<Concurrency::timer<CommandMessage::Message>> m_RefreshTimer;
    std::unique_ptr<Concurrency::timer<CommandMessage::Message>> m_KillerTimer;

    std::vector<std::shared_ptr<Recipient>> m_Recipients;

    static std::wregex g_WinVerRegEx;

    std::shared_ptr<ByteStream> m_configStream;
    std::shared_ptr<ByteStream> m_localConfigStream;

public:
    HRESULT SetArchiveName(const std::wstring& strArchiveName)
    {
        if (strArchiveName.empty())
            return E_INVALIDARG;
        m_strArchiveName = strArchiveName;
        return S_OK;
    };

    HRESULT SetOutput(const OutputSpec& output, const OutputSpec& temporary)
    {
        if (output.Type != OutputSpec::Kind::Directory)
            return E_INVALIDARG;
        m_Output = output;
        if (temporary.Type != OutputSpec::Kind::Directory)
            return E_INVALIDARG;
        m_Temporary = temporary;

        return S_OK;
    };

    HRESULT SetConfigStreams(const std::shared_ptr<ByteStream>& stream, const std::shared_ptr<ByteStream>& localstream)
    {
        if (stream != nullptr)
        {
            stream->SetFilePointer(0LL, FILE_BEGIN, NULL);
            m_configStream = stream;
        }
        if (localstream != nullptr)
        {
            localstream->SetFilePointer(0LL, FILE_BEGIN, NULL);
            m_localConfigStream = localstream;
        }
        return S_OK;
    }

    HRESULT SetRecipients(const std::vector<std::shared_ptr<WolfExecution::Recipient>> recipients);

    HRESULT BuildFullArchiveName();

    HRESULT CreateArchiveAgent();
    HRESULT CreateCommandAgent(boost::tribool bChildDebug, std::chrono::milliseconds msRefresh, DWORD dwMaxTasks);

    bool UseJournalWhenEncrypting() const { return m_bUseJournalWhenEncrypting; };
    void SetUseJournalWhenEncrypting(bool bUseJournalWhenEncrypting)
    {
        m_bUseJournalWhenEncrypting = bUseJournalWhenEncrypting;
    };

    bool TeeClearTextOutput() const { return m_bTeeClearTextOutput; };
    void SetTeeClearTextOutput(bool bTeeClearTextOutput) { m_bTeeClearTextOutput = bTeeClearTextOutput; };

    HRESULT SetJobTimeOutFromConfig(
        const ConfigItem& item,
        std::chrono::milliseconds dwCmdTimeOut,
        std::chrono::milliseconds dwArchiveTimeOut);
    HRESULT SetJobConfigFromConfig(const ConfigItem& item);
    HRESULT SetCommandsFromConfig(const ConfigItem& item);
    HRESULT SetRestrictionsFromConfig(const ConfigItem& item);
    HRESULT SetRepeatBehaviourFromConfig(const ConfigItem& item);
    HRESULT SetRepeatBehaviour(const Repeat behavior);
    HRESULT SetCompressionLevel(const std::wstring& strCompressionLevel);

    void SetOptional() { m_bOptional = true; }
    void SetMandatory() { m_bOptional = false; }
    bool IsOptional() const { return m_bOptional; }

    bool ShouldUpload() const
    {
        if (m_Output.UploadOutput && m_Output.UploadOutput->IsFileUploaded(m_strOutputFileName))
            return true;
        return false;
    }

    void SetChildDebug() { m_bChildDebug = true; }
    void UnSetChildDebug() { m_bChildDebug = false; }

    bool IsChildDebugActive(boost::tribool bGlobalSetting) const
    {
        if (boost::indeterminate(bGlobalSetting))
        {
            return (bool)m_bChildDebug;
        }
        else
        {
            return (bool)bGlobalSetting;
        }
    }

    Repeat RepeatBehaviour() const { return m_RepeatBehavior; };

    const std::wstring& GetKeyword() const { return m_commandSet; };
    DWORD GetConcurrency() const { return m_dwConcurrency; };
    const std::wstring& GetFullArchiveName() const { return m_strArchiveFullPath; };
    const std::wstring& GetArchiveFileName() const { return m_strArchiveFileName; };
    const std::wstring& GetOutputFullPath() const { return m_strOutputFullPath; };
    const std::wstring& GetOutputFileName() const { return m_strOutputFileName; };
    const std::vector<CommandMessage::Message>& GetCommands() const { return m_Commands; };

    std::vector<std::shared_ptr<Recipient>>& Recipients() { return m_Recipients; };
    const std::vector<std::shared_ptr<Recipient>>& Recipients() const { return m_Recipients; };

    HRESULT EnqueueCommands();

    HRESULT CompleteExecution();
    HRESULT TerminateAllAndComplete();

    HRESULT CompleteArchive(UploadMessage::ITarget* pUploadMessageQueue);

    WolfExecution(Journal& journal, Wolf::Outcome::Outcome& outcome)
        : m_journal(journal)
        , m_outcome(outcome)
    {
    }

    ~WolfExecution();
};
}  // namespace Command::Wolf
}  // namespace Orc

#pragma managed(pop)
