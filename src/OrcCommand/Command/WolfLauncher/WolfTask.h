//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "CommandExecute.h"

#include "CommandNotification.h"
#include "CommandMessage.h"

#include "Output/Console/Journal.h"

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

private:
    Orc::Command::Output::Journal& m_journal;
    std::wstring m_commandSet;
    std::wstring m_command;

    DWORD m_dwPID = 0L;
    DWORD m_dwExitCode = 0L;

    DWORD m_dwLastReportedHang = 30;
    DWORD m_dwMostReportedHang = 0;

    FILETIME m_StartTime;
    FILETIME m_LastActiveTime;

    PROCESS_TIMES m_Times;

    Status m_Status = Init;

public:
    WolfTask() = delete;

    WolfTask(const std::wstring& commandSet, const std::wstring& command, Command::Output::Journal& journal)
        : m_journal(journal)
        , m_commandSet(commandSet)
        , m_command(command)
    {
        ZeroMemory(&m_Times, sizeof(PROCESS_TIMES));
        ZeroMemory(&m_LastActiveTime, sizeof(FILETIME));
        ZeroMemory(&m_StartTime, sizeof(FILETIME));
    };

    HRESULT ApplyNotification(
        const std::shared_ptr<CommandNotification>& notification,
        std::vector<std::shared_ptr<CommandMessage>>& actions);

    ~WolfTask();
};
}  // namespace Command::Wolf
}  // namespace Orc
#pragma managed(pop)
