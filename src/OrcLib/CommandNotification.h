//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <memory>
#include <agents.h>

#include "CommandExecute.h"

#pragma managed(push, off)

namespace Orc {

struct PROCESS_TIMES
{
    FILETIME CreationTime;
    FILETIME ExitTime;
    FILETIME KernelTime;
    FILETIME UserTime;
};

using PPROCESS_TIMES = PROCESS_TIMES*;

struct JOB_STATISTICS
{
    LARGE_INTEGER TotalUserTime;
    LARGE_INTEGER TotalKernelTime;
    DWORD TotalPageFaultCount;
    DWORD TotalProcesses;
    DWORD ActiveProcesses;
    DWORD TotalTerminatedProcesses;
    IO_COUNTERS IoInfo;
    SIZE_T PeakProcessMemoryUsed;
    SIZE_T PeakJobMemoryUsed;
};
using PJOB_STATISTICS = JOB_STATISTICS*;

class CommandNotification
{

public:
    enum Result
    {
        Information,
        Success,
        Failure
    };

    enum Event
    {
        Started,
        Terminated,
        Canceled,
        AllTerminated,
        Running,
        JobTimeLimit,
        JobProcessLimit,
        JobMemoryLimit,
        JobEmpty,
        ProcessTimeLimit,
        ProcessAbnormalTermination,
        ProcessMemoryLimit,
        Done
    };

    using Ptr = std::shared_ptr<CommandNotification>;
    using Notification = std::shared_ptr<CommandNotification>;
    using UnboundedMessageBuffer = Concurrency::unbounded_buffer<Notification>;

    using ITarget = Concurrency::ITarget<Notification>;
    using ISource = Concurrency::ISource<Notification>;

private:
    Result m_Result;
    Event m_Event;
    DWORD_PTR m_dwPid;
    std::wstring m_Keyword;
    HRESULT m_hr;

    // Process Start information
    FILETIME m_ProcessStartTime;

    // Process Termination information
    DWORD m_ExitCode;
    PPROCESS_TIMES m_pProcessTimes;
    PIO_COUNTERS m_pIoCounters;

    PJOB_STATISTICS m_pJobStats;

    std::wstring m_commandLine;
    std::optional<std::wstring> m_originResourceName;
    std::optional<std::wstring> m_originFriendlyName;
    std::optional<std::wstring> m_executableSha1;
    bool m_isSelfOrcExecutable;

protected:
    CommandNotification(Event result);

public:
    static Notification
    NotifyStarted(DWORD dwPid, const std::wstring& Keyword, const HANDLE hProcess, const std::wstring& commandLine);

    std::optional<std::wstring> GetOriginFriendlyName() const { return m_originFriendlyName; }
    void SetOriginFriendlyName(const std::optional<std::wstring>& name) { m_originFriendlyName = name; }

    std::optional<std::wstring> GetOriginResourceName() const { return m_originResourceName; }
    void SetOriginResourceName(const std::optional<std::wstring>& name) { m_originResourceName = name; }

    std::optional<std::wstring> GetExecutableSha1() const { return m_executableSha1; }
    void SetExecutableSha1(const std::optional<std::wstring>& sha1) { m_executableSha1 = sha1; }

    bool IsSelfOrcExecutable() const { return m_isSelfOrcExecutable; }
    void SetIsSelfOrcExecutable(bool value) { m_isSelfOrcExecutable = value; }

    static Notification NotifyProcessTerminated(DWORD dwPid, const std::wstring& Keyword, const HANDLE hProcess);

    // Job Notifictions
    static Notification NotifyJobEmpty();
    static Notification NotifyJobTimeLimit();
    static Notification NotifyJobMemoryLimit();
    static Notification NotifyJobProcessLimit();

    // Job Notifications for Process
    static Notification NotifyProcessTimeLimit(DWORD_PTR dwPid);
    static Notification NotifyProcessMemoryLimit(DWORD_PTR dwPid);
    static Notification NotifyProcessAbnormalTermination(DWORD_PTR dwPid);

    // Running List notifications
    static Notification NotifyRunningProcess(const std::wstring& keyword, DWORD dwPid);
    static Notification NotifyRunningProcess(std::wstring&& keyword, DWORD dwPid);

    // Agent notifications
    static Notification NotifyCanceled();
    static Notification NotifyTerminateAll();
    static Notification NotifyDone(const std::wstring& keyword, const HANDLE hJob);

    static Notification NotifyFailure(Event anevent, HRESULT hr, DWORD dwPid, const std::wstring& Keyword);

    // General notification properties
    DWORD_PTR GetProcessID() const { return m_dwPid; };
    Event GetEvent() const { return m_Event; };
    const std::wstring& GetKeyword() const { return m_Keyword; };
    HRESULT GetHResult() const { return m_hr; };

    // Process start notification properties
    FILETIME GetStartTime() const
    {
        if (m_Event == Started)
            return m_ProcessStartTime;
        FILETIME retval;
        retval.dwHighDateTime = retval.dwLowDateTime = 0L;
        return retval;
    }

    // Process Termination notification properties
    DWORD GetExitCode() const
    {
        if (m_Event == Terminated)
            return m_ExitCode;
        return (DWORD)-1;
    };
    const PPROCESS_TIMES GetProcessTimes() const
    {
        if (m_Event == Terminated)
            return m_pProcessTimes;
        return nullptr;
    };
    const PIO_COUNTERS GetProcessIoCounters() const
    {
        if (m_Event == Terminated)
            return m_pIoCounters;
        return nullptr;
    };

    // Job Statistics
    const PJOB_STATISTICS GetJobStatictics() const
    {
        if (m_Event == Done)
            return m_pJobStats;
        return nullptr;
    };

    const std::wstring& GetProcessCommandLine() const { return m_commandLine; }

    ~CommandNotification(void);
};
}  // namespace Orc
#pragma managed(pop)
