//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include <string>
#include <regex>
#include <memory>
#include <concrt.h>

#include "OrcLib.h"

#include "ProcessRedirect.h"
#include "DebugAgent.h"

#pragma managed(push, off)

namespace Orc {

class JobObject;

class OnComplete
{

public:
    typedef enum _Action
    {
        Void,
        Delete,
        Archive,
        ArchiveAndDelete,
        FlushArchiveQueue
    } Action;

    typedef enum _Object
    {
        File,
        Directory,
        Stream,
        ArchiveQueue
    } Object;

private:
    class OnCompleteTerminationHandler : public TerminationHandler
    {
    public:
        OnCompleteTerminationHandler(
            const std::wstring& strDescr,
            const OnComplete::Object obj,
            const std::wstring& fullpath)
            : TerminationHandler(strDescr, ROBUSTNESS_TEMPFILE)
            , m_object(obj)
            , m_FullPath(fullpath) {};

        OnCompleteTerminationHandler(
            const std::wstring& strDescr,
            const OnComplete::Object obj,
            const std::shared_ptr<ByteStream>& stream)
            : TerminationHandler(strDescr, ROBUSTNESS_TEMPFILE)
            , m_object(obj)
            , m_Stream(stream) {};

        HRESULT operator()();

        ~OnCompleteTerminationHandler() {};

    private:
        std::wstring m_FullPath;
        std::shared_ptr<ByteStream> m_Stream;
        OnComplete::Object m_object;
    };

public:
    OnComplete(Action action, ArchiveMessage::ITarget* pArchive = nullptr);
    OnComplete(
        Action action,
        const std::wstring& name,
        const std::wstring& fullpath,
        ArchiveMessage::ITarget* pArchive = nullptr);
    OnComplete(
        Action action,
        const std::wstring& name,
        const std::wstring& fullpath,
        const std::wstring& matchPattern,
        ArchiveMessage::ITarget* pArchive = nullptr);
    OnComplete(
        Action action,
        const std::wstring& name,
        const std::shared_ptr<ByteStream>& stream,
        ArchiveMessage::ITarget* pArchive = nullptr);

    OnComplete(OnComplete&& other)
    {
        m_action = other.m_action;
        std::swap(m_Name, other.m_Name);
        std::swap(m_FullPath, other.m_FullPath);
        std::swap(m_Pattern, other.m_Pattern);
        m_pArchive = other.m_pArchive;
        other.m_pArchive = nullptr;
        m_pTerminationHandler = other.m_pTerminationHandler;
        other.m_pTerminationHandler = nullptr;
    }
    ~OnComplete();

    Action GetAction() const { return m_action; };
    Object GetObjectType() const { return m_object; };

    bool DeleteWhenDone() const
    {
        if (m_action == Delete || m_action == ArchiveAndDelete)
            return true;
        return false;
    };

    const std::wstring& Name() const { return m_Name; };
    const std::wstring& Fullpath() const { return m_FullPath; };
    const std::wstring& MatchPattern() const { return m_Pattern; };

    const std::shared_ptr<ByteStream>& GetStream() const { return m_stream; };

    ArchiveMessage::ITarget* ArchiveTarget() const { return m_pArchive; };

    void CancelTerminationHandler();

private:
    OnComplete(const OnComplete& other) { DBG_UNREFERENCED_PARAMETER(other); };
    Action m_action;
    std::wstring m_Name;

    Object m_object;
    std::wstring m_FullPath;
    std::wstring m_Pattern;  // used to match files in an output directory

    std::shared_ptr<ByteStream> m_stream;

    ArchiveMessage::ITarget* m_pArchive;

    concurrency::critical_section m_cs;
    std::shared_ptr<OnCompleteTerminationHandler> m_pTerminationHandler;
};

class CommandExecute
{
    friend class CommandAgent;

public:
    typedef enum _ProcessStatus
    {
        Initialized,
        Created,
        Started,
        Complete,
        Closed
    } ProcessStatus;

public:
    CommandExecute(const std::wstring& Keyword);

    HRESULT AddRedirection(const std::shared_ptr<ProcessRedirect>& redirect);

    HRESULT AddArgument(const std::wstring& arg, LONG OrderId);

    HRESULT AddExecutableToRun(const std::wstring& szImageFilePath);

    HRESULT AddOnCompleteAction(std::shared_ptr<OnComplete>&& action)
    {
        m_OnCompleteActions.push_back(action);
        return S_OK;
    };

    HRESULT AddDumpFileDirectory(const std::wstring& strDirectory);

    HRESULT Execute(const JobObject& job, bool bBreakAway = true);
    HRESULT CreateChildProcess(const JobObject& job, bool bBreakAway = true);
    HRESULT ResumeChildProcess();

    HANDLE ProcessHandle() { return m_pi.hProcess; };

    virtual HRESULT WaitCompletion(DWORD dwTimeOut = INFINITE);
    virtual bool HasCompleted();

    virtual DWORD ProcessID() const { return m_pi.dwProcessId; };
    virtual DWORD ExitCode() const { return m_dwExitCode; };

    virtual HRESULT CompleteExecution(ArchiveMessage::ITarget* pCab = nullptr);

    ProcessRedirect::RedirectStatus EvaluateRedirectionsStatus();

    const std::wstring& GetKeyword() const { return m_Keyword; };

    ProcessStatus GetStatus()
    {
        Concurrency::critical_section::scoped_lock sl(m_cs);
        return m_Status;
    };
    const std::vector<std::shared_ptr<OnComplete>>& GetOnCompleteActions() { return m_OnCompleteActions; }

    const std::optional<std::wstring>& GetOriginResourceName() const { return m_originResourceName; }
    void SetOriginResourceName(const std::wstring& name) { m_originResourceName = name; }

    const std::optional<std::wstring>& GetOriginFriendlyName() const { return m_originFriendlyName; }
    void SetOriginFriendlyName(const std::wstring& name) { m_originFriendlyName = name; }

    const std::optional<std::wstring>& GetExecutableSha1() const { return m_executableSha1; }
    void SetExecutableSha1(const std::wstring& sha1) { m_executableSha1 = sha1; }

    bool IsSelfOrcExecutable() const { return m_isSelfOrcExecutable; }
    void SetIsSelfOrcExecutable(bool value) { m_isSelfOrcExecutable = value; }

    const std::optional<std::wstring>& GetOrcTool() const { return m_orcTool; }
    void SetOrcTool(const std::optional<std::wstring>& tool) { m_orcTool = tool; }

    const std::optional<std::chrono::milliseconds>& GetTimeout() const { return m_timeout; }

    void SetTimeoutTimer(std::shared_ptr<void> timer) { m_timeoutTimer = std::move(timer); }

    const std::optional<uint64_t>& DiskFreeSpaceRequirement() const { return m_diskFreeSpaceRequirement; }
    void SetDiskFreeSpaceRequirement(uint64_t value) { m_diskFreeSpaceRequirement = value; }

    ~CommandExecute(void);

private:
    ProcessStatus m_Status;
    DWORD m_dwExitCode;
    std::wstring m_commandLine;
    STARTUPINFO m_si;
    PROCESS_INFORMATION m_pi;
    std::optional<std::chrono::milliseconds> m_timeout;
    std::shared_ptr<void> m_timeoutTimer;
    std::optional<std::wstring> m_originResourceName;
    std::optional<std::wstring> m_originFriendlyName;
    std::optional<std::wstring> m_executableSha1;
    std::optional<std::wstring> m_orcTool;
    bool m_isSelfOrcExecutable;

    std::vector<std::shared_ptr<OnComplete>> m_OnCompleteActions;

    ProcessRedirect::RedirectStatus m_RedirectStatus;
    std::vector<std::shared_ptr<ProcessRedirect>> m_Redirections;

    std::shared_ptr<DebugAgent> m_pDebugger;

    std::wstring m_Keyword;

    std::vector<std::pair<std::wstring, LONG>> m_Arguments;
    std::wstring m_ImageFilePath;
    std::wstring m_DumpFilePath;
    std::optional<uint64_t> m_diskFreeSpaceRequirement;

    Concurrency::critical_section m_cs;

    void SetStatus(ProcessStatus status)
    {
        Concurrency::critical_section::scoped_lock sl(m_cs);
        if (m_Status >= status)
            return;
        else
        {
            m_Status = status;
        }
    }

    HANDLE GetChildHandleFor(ProcessRedirect::ProcessInOut selection);
    HANDLE GetParentHandleFor(ProcessRedirect::ProcessInOut selection);

    static VOID CALLBACK WaitOrTimerCallbackFunction(__in PVOID lpParameter, __in BOOLEAN TimerOrWaitFired);
};

}  // namespace Orc

#pragma managed(pop)
