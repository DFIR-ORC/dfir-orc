//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "WolfTask.h"
#include "Utils/Result.h"

using namespace Orc;
using namespace Orc::Command::Wolf;

HRESULT WolfTask::ApplyNotification(
    const std::shared_ptr<CommandNotification>& notification,
    std::vector<std::shared_ptr<CommandMessage>>& actions)
{
    if (notification == nullptr)
        return E_POINTER;

    switch (notification->GetEvent())
    {
        case CommandNotification::Started:
            m_journal.Print(
                m_commandSet, m_command, L"Started (pid: {})", m_dwPID == 0 ? notification->GetProcessID() : m_dwPID);

            m_dwPID = static_cast<DWORD>(notification->GetProcessID());
            m_StartTime = notification->GetStartTime();
            m_Status = Running;

            break;
        case CommandNotification::Terminated:
            if (notification->GetExitCode() == 0)
            {
                m_journal.Print(
                    m_commandSet,
                    m_command,
                    L"Successfully terminated (pid: {})",
                    m_dwPID == 0 ? notification->GetProcessID() : m_dwPID);
            }
            else
            {
                m_journal.Print(
                    m_commandSet,
                    m_command,
                    L"Terminated with an error (pid: {}, exit code: {:#x})",
                    m_dwPID == 0 ? notification->GetProcessID() : m_dwPID,
                    notification->GetExitCode());
            }

            m_dwExitCode = notification->GetExitCode();
            m_Status = Done;
            break;
        case CommandNotification::Canceled:
            m_Status = Cancelled;
            break;
        case CommandNotification::Running:
            // Process is still running, checking if it hangs...
            {
                HANDLE hProcess =
                    OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, static_cast<DWORD>(notification->GetProcessID()));

                if (NULL != hProcess)
                {
                    PROCESS_TIMES times;
                    if (GetProcessTimes(
                            hProcess, &times.CreationTime, &times.ExitTime, &times.KernelTime, &times.UserTime))
                    {
                        bool bChanged = false;
                        if (times.KernelTime.dwHighDateTime > m_Times.KernelTime.dwHighDateTime)
                        {
                            m_Times.KernelTime.dwHighDateTime = times.KernelTime.dwHighDateTime;
                            bChanged = true;
                        }
                        if (times.KernelTime.dwLowDateTime > m_Times.KernelTime.dwLowDateTime)
                        {
                            m_Times.KernelTime.dwHighDateTime = times.KernelTime.dwHighDateTime;
                            bChanged = true;
                        }
                        if (times.UserTime.dwHighDateTime > m_Times.UserTime.dwHighDateTime)
                        {
                            m_Times.UserTime.dwHighDateTime = times.UserTime.dwHighDateTime;
                            bChanged = true;
                        }
                        if (times.UserTime.dwLowDateTime > m_Times.UserTime.dwLowDateTime)
                        {
                            m_Times.UserTime.dwHighDateTime = times.UserTime.dwHighDateTime;
                            bChanged = true;
                        }

                        if (bChanged || (m_LastActiveTime.dwHighDateTime == 0 && m_LastActiveTime.dwLowDateTime == 0))
                        {
                            GetSystemTimeAsFileTime(&m_LastActiveTime);
                        }
                        else
                        {
                            FILETIME NowTime;
                            GetSystemTimeAsFileTime(&NowTime);
                            DWORD dwHangTime =
                                (NowTime.dwLowDateTime - m_LastActiveTime.dwLowDateTime) / (10000 * 1000);
                            if (dwHangTime > 0)
                            {
                                if (dwHangTime - m_dwLastReportedHang >= 30)
                                {
                                    Log::Error(
                                        L"{} (pid: {}): Hanged for {} secs",
                                        m_command,
                                        m_dwPID == 0 ? notification->GetProcessID() : m_dwPID,
                                        dwHangTime);
                                    m_dwLastReportedHang = dwHangTime;
                                }
                                m_dwMostReportedHang = std::max(m_dwMostReportedHang, dwHangTime);
                            }
                            else
                            {
                                Log::Debug(
                                    L"{} (pid: {}): Process consumed {} msecs",
                                    m_command,
                                    m_dwPID == 0 ? notification->GetProcessID() : m_dwPID,
                                    (NowTime.dwLowDateTime - m_LastActiveTime.dwLowDateTime) / (10000));
                            }
                        }
                    }
                    CloseHandle(hProcess);
                }
            }
            break;
        case CommandNotification::ProcessTimeLimit:
            // Process has reached its time limit, kill it!
            Log::Critical(
                L"{} (pid: {}): CPU Time limit, it will now be terminated",
                m_command,
                m_dwPID == 0 ? notification->GetProcessID() : m_dwPID);

            actions.push_back(CommandMessage::MakeTerminateMessage(
                static_cast<DWORD>(static_cast<DWORD64>(notification->GetProcessID()))));

            m_Status = Failed;
            break;
        case CommandNotification::ProcessAbnormalTermination:
            m_dwExitCode = notification->GetExitCode();

            Log::Critical(
                L"{} (pid: {}): Abnormal termination [{}]",
                m_command,
                m_dwPID == 0 ? notification->GetProcessID() : m_dwPID,
                Win32Error(m_dwExitCode));

            m_Status = Failed;
            break;
        case CommandNotification::ProcessMemoryLimit:
            Log::Critical(
                L"{} (pid: {}): Memory limit, it will now be terminated",
                m_command,
                m_dwPID == 0 ? notification->GetProcessID() : m_dwPID);
            // Process has reached its memory limit, kill it!
            actions.push_back(CommandMessage::MakeTerminateMessage(m_dwPID));
            m_Status = Failed;
            break;
        case CommandNotification::AllTerminated:
            Log::Critical(
                L"{} (pid: {}): Memory limit, it will now be terminated",
                m_command,
                m_dwPID == 0 ? notification->GetProcessID() : m_dwPID);
            m_Status = Failed;
            break;
        case CommandNotification::JobTimeLimit:
        case CommandNotification::JobProcessLimit:
        case CommandNotification::JobMemoryLimit:
        case CommandNotification::JobEmpty:
        case CommandNotification::Done:
        default:
            // These "job" related ntofication do not relate to a single WolfTask
            // We'll simply ignore them
            break;
    }
    return S_OK;
}

WolfTask::~WolfTask(void) {}
