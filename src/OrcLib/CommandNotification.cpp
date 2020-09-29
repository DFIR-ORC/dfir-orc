//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include <memory>

#include "CommandNotification.h"

using namespace Orc;

namespace {

// Enable the use of std::make_shared with CommandNotification protected constructor
struct CommandNotificationT : public Orc::CommandNotification
{
    template <typename... Args>
    inline CommandNotificationT(Args&&... args)
        : CommandNotification(std::forward<Args>(args)...)
    {
    }
};

}  // namespace

CommandNotification::CommandNotification(CommandNotification::Event anevent)
    : m_Event(anevent)
    , m_hr(S_OK)
    , m_dwPid(0L)
    , m_ExitCode(0)
    , m_pIoCounters(nullptr)
    , m_pProcessTimes(nullptr)
    , m_pJobStats(nullptr)
{
}

CommandNotification::Notification
CommandNotification::NotifyStarted(DWORD dwPid, const std::wstring& Keyword, HANDLE hProcess)
{
    auto retval = std::make_shared<::CommandNotificationT>(CommandNotification::Started);

    retval->m_Result = CommandNotification::Success;
    retval->m_dwPid = dwPid;
    retval->m_Keyword = Keyword;

    if (hProcess != INVALID_HANDLE_VALUE)
    {
        FILETIME ExitTime, KernelTime, UserTime;
        if (!::GetProcessTimes(hProcess, &retval->m_ProcessStartTime, &ExitTime, &UserTime, &KernelTime))
            retval->m_ProcessStartTime = FILETIME();
    }

    return retval;
}

CommandNotification::Notification
CommandNotification::NotifyProcessTerminated(DWORD dwPid, const std::wstring& Keyword, const HANDLE hProcess)
{
    auto retval = std::make_shared<::CommandNotificationT>(CommandNotification::Terminated);

    retval->m_Result = CommandNotification::Success;
    retval->m_dwPid = dwPid;
    retval->m_Keyword = Keyword;

    if (hProcess != INVALID_HANDLE_VALUE)
    {
        HRESULT hr = E_FAIL;
        if (!GetExitCodeProcess(hProcess, &retval->m_ExitCode))
            hr = HRESULT_FROM_WIN32(GetLastError());

        retval->m_pProcessTimes = (PPROCESS_TIMES)malloc(sizeof(PROCESS_TIMES));

        if (!::GetProcessTimes(
                hProcess,
                &retval->m_pProcessTimes->CreationTime,
                &retval->m_pProcessTimes->ExitTime,
                &retval->m_pProcessTimes->KernelTime,
                &retval->m_pProcessTimes->UserTime))
            retval->m_hr = HRESULT_FROM_WIN32(GetLastError());

        retval->m_pIoCounters = (PIO_COUNTERS)malloc(sizeof(IO_COUNTERS));

        if (!::GetProcessIoCounters(hProcess, retval->m_pIoCounters))
            retval->m_hr = HRESULT_FROM_WIN32(GetLastError());
    }

    return retval;
}

// Job Notifictions
CommandNotification::Notification CommandNotification::NotifyJobEmpty()
{
    auto retval = std::make_shared<::CommandNotificationT>(CommandNotification::JobEmpty);

    retval->m_Result = Information;

    return retval;
}

CommandNotification::Notification CommandNotification::NotifyJobTimeLimit()
{
    auto retval = std::make_shared<::CommandNotificationT>(CommandNotification::JobTimeLimit);

    retval->m_Result = Information;

    return retval;
}

CommandNotification::Notification CommandNotification::NotifyJobMemoryLimit()
{
    auto retval = std::make_shared<::CommandNotificationT>(CommandNotification::JobMemoryLimit);

    retval->m_Result = Information;

    return retval;
}

CommandNotification::Notification CommandNotification::NotifyJobProcessLimit()
{
    auto retval = std::make_shared<::CommandNotificationT>(CommandNotification::JobProcessLimit);

    retval->m_Result = Information;

    return retval;
}

// Job Notifications for Process
CommandNotification::Notification CommandNotification::NotifyProcessTimeLimit(DWORD_PTR dwPid)
{
    auto retval = std::make_shared<::CommandNotificationT>(CommandNotification::ProcessTimeLimit);

    retval->m_dwPid = dwPid;
    retval->m_Result = Information;
    return retval;
}

CommandNotification::Notification CommandNotification::NotifyProcessMemoryLimit(DWORD_PTR dwPid)
{
    auto retval = std::make_shared<::CommandNotificationT>(CommandNotification::ProcessMemoryLimit);

    retval->m_dwPid = dwPid;
    retval->m_Result = Information;
    return retval;
}

CommandNotification::Notification CommandNotification::NotifyProcessAbnormalTermination(DWORD_PTR dwPid)
{
    auto retval = std::make_shared<::CommandNotificationT>(CommandNotification::ProcessAbnormalTermination);

    retval->m_dwPid = dwPid;
    retval->m_Result = Failure;

    HANDLE hProcess = INVALID_HANDLE_VALUE;
    if ((hProcess = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)dwPid)) != NULL)
    {
        GetExitCodeProcess(hProcess, &retval->m_ExitCode);
        CloseHandle(hProcess);
    }

    return retval;
}

CommandNotification::Notification CommandNotification::NotifyRunningProcess(const std::wstring& keyword, DWORD dwPid)
{
    auto retval = std::make_shared<::CommandNotificationT>(CommandNotification::Running);

    retval->m_Keyword = keyword;
    retval->m_dwPid = dwPid;
    retval->m_Result = Information;

    return retval;
}

CommandNotification::Notification CommandNotification::NotifyRunningProcess(std::wstring&& keyword, DWORD dwPid)
{
    auto retval = std::make_shared<::CommandNotificationT>(CommandNotification::Running);

    std::swap(retval->m_Keyword, keyword);
    retval->m_dwPid = dwPid;
    retval->m_Result = Information;

    return retval;
}

CommandNotification::Notification CommandNotification::NotifyCanceled()
{
    auto retval = std::make_shared<::CommandNotificationT>(CommandNotification::Canceled);

    retval->m_Result = CommandNotification::Success;

    return retval;
}

CommandNotification::Notification CommandNotification::NotifyTerminateAll()
{
    auto retval = std::make_shared<::CommandNotificationT>(CommandNotification::AllTerminated);

    retval->m_Result = CommandNotification::Success;
    return retval;
}

CommandNotification::Notification CommandNotification::NotifyDone(const std::wstring& keyword, const HANDLE hJob)
{
    auto retval = std::make_shared<::CommandNotificationT>(CommandNotification::Done);

    retval->m_Keyword = keyword;
    retval->m_Result = CommandNotification::Success;

    PJOB_STATISTICS pJobStats = (PJOB_STATISTICS)malloc(sizeof(JOB_STATISTICS));
    if (pJobStats == nullptr)
        return retval;

    ZeroMemory(pJobStats, sizeof(JOB_STATISTICS));

    JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION BasicIoInfo;
    ZeroMemory(&BasicIoInfo, sizeof(JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION));

    DWORD dwReturnedBytes;
    if (!QueryInformationJobObject(
            hJob,
            JobObjectBasicAndIoAccountingInformation,
            &BasicIoInfo,
            sizeof(JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION),
            &dwReturnedBytes))
    {
        free(pJobStats);
        return retval;
    }
    pJobStats->TotalUserTime = BasicIoInfo.BasicInfo.TotalUserTime;
    pJobStats->TotalKernelTime = BasicIoInfo.BasicInfo.TotalKernelTime;
    pJobStats->TotalPageFaultCount = BasicIoInfo.BasicInfo.TotalPageFaultCount;
    pJobStats->TotalProcesses = BasicIoInfo.BasicInfo.TotalProcesses;
    pJobStats->ActiveProcesses = BasicIoInfo.BasicInfo.ActiveProcesses;
    pJobStats->TotalTerminatedProcesses = BasicIoInfo.BasicInfo.TotalTerminatedProcesses;

    CopyMemory(&(pJobStats->IoInfo), &BasicIoInfo.IoInfo, sizeof(IO_COUNTERS));

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION ExtendedInfo;
    if (!QueryInformationJobObject(
            hJob,
            JobObjectExtendedLimitInformation,
            &ExtendedInfo,
            sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION),
            &dwReturnedBytes))
    {
        free(pJobStats);
        return retval;
    }
    pJobStats->PeakProcessMemoryUsed = ExtendedInfo.PeakProcessMemoryUsed;
    pJobStats->PeakJobMemoryUsed = ExtendedInfo.ProcessMemoryLimit;
    retval->m_pJobStats = pJobStats;
    return retval;
}

CommandNotification::Notification CommandNotification::NotifyFailure(
    CommandNotification::Event anevent,
    HRESULT hr,
    DWORD dwPid,
    const std::wstring& Keyword)
{
    auto retval = std::make_shared<::CommandNotificationT>(anevent);

    retval->m_hr = hr;
    retval->m_Result = CommandNotification::Failure;
    retval->m_dwPid = dwPid;
    retval->m_Keyword = Keyword;

    return retval;
}

CommandNotification::~CommandNotification(void)
{
    if (m_pIoCounters)
        free(m_pIoCounters);
    m_pIoCounters = nullptr;

    if (m_pProcessTimes)
        free(m_pProcessTimes);
    m_pProcessTimes = nullptr;

    if (m_pJobStats)
        free(m_pJobStats);
    m_pJobStats = nullptr;
}
