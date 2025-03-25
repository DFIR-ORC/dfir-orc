//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "CommandExecute.h"

#include "CommandNotification.h"
#include "CommandMessage.h"

#include "Command/WolfLauncher/Journal.h"

#pragma managed(push, off)

namespace Orc {

namespace Command::Wolf {

class WolfTask
{
public:
    typedef enum _Status
    {
        Init = 0,
        Running,
        Cancelled,
        Stalled,
        Failed,
        Dumped,
        Done
    } Status;

    WolfTask(const std::wstring& commandSet, const std::wstring& command, Journal& journal)
        : m_journal(journal)
        , m_commandSet(commandSet)
        , m_command(command)
        , m_isSelfOrcExecutable(false)
        , m_dwPID(0)
        , m_dwExitCode(-1)
        , m_dwLastReportedHang(30)
        , m_dwMostReportedHang(0)
        , m_userTime()
        , m_kernelTime()
        , m_status(Init)
    {
        ZeroMemory(&m_times, sizeof(PROCESS_TIMES));
        ZeroMemory(&m_lastActiveTime, sizeof(FILETIME));
        ZeroMemory(&m_startTime, sizeof(FILETIME));
        ZeroMemory(&m_creationTime, sizeof(FILETIME));
        ZeroMemory(&m_exitTime, sizeof(FILETIME));
    };

    const std::wstring& Command() const { return m_command; }
    const std::wstring& CommandSet() const { return m_commandSet; }
    DWORD Pid() const { return m_dwPID; }
    DWORD ExitCode() const { return m_dwExitCode; }
    const std::wstring& CommandLine() const { return m_commandLine; }

    FILETIME CreationTime() const { return m_creationTime; }
    FILETIME ExitTime() const { return m_exitTime; }

    std::optional<std::chrono::microseconds> UserTime() const { return m_userTime; }
    std::optional<std::chrono::microseconds> KernelTime() const { return m_kernelTime; }
    std::optional<IO_COUNTERS> IoCounters() const { return m_ioCounters; }

    std::optional<std::wstring> OriginResourceName() const { return m_originResourceName; }
    std::optional<std::wstring> OriginFriendlyName() const { return m_originFriendlyName; }
    std::optional<std::wstring> ExecutableSha1() const { return m_executableSha1; }
    std::optional<std::wstring> OrcTool() const { return m_orcTool; }
    bool IsSelfOrcExecutable() const { return m_isSelfOrcExecutable; }

    HRESULT ApplyNotification(
        const std::shared_ptr<CommandNotification>& notification,
        std::vector<std::shared_ptr<CommandMessage>>& actions);

private:
    Journal& m_journal;

    std::wstring m_commandSet;
    std::wstring m_command;
    std::wstring m_commandLine;
    std::optional<std::wstring> m_originResourceName;
    std::optional<std::wstring> m_originFriendlyName;
    std::optional<std::wstring> m_executableSha1;
    std::optional<std::wstring> m_orcTool;
    bool m_isSelfOrcExecutable;

    DWORD m_dwPID;
    DWORD m_dwExitCode;

    DWORD m_dwLastReportedHang;
    DWORD m_dwMostReportedHang;

    FILETIME m_startTime;
    FILETIME m_lastActiveTime;

    FILETIME m_creationTime;
    FILETIME m_exitTime;

    std::optional<std::chrono::microseconds> m_userTime;
    std::optional<std::chrono::microseconds> m_kernelTime;

    std::optional<IO_COUNTERS> m_ioCounters;

    Status m_status;

    PROCESS_TIMES m_times;
};

}  // namespace Command::Wolf
}  // namespace Orc

#pragma managed(pop)
