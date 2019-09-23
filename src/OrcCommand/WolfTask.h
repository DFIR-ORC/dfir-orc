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

#pragma managed(push, off)

namespace Orc {

class LogFileWriter;

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
    std::wstring m_cmdKeyword;

    DWORD m_dwPID = 0L;
    DWORD m_dwExitCode = 0L;

    DWORD m_dwLastReportedHang = 30;
    DWORD m_dwMostReportedHang = 0;

    FILETIME m_StartTime;
    FILETIME m_LastActiveTime;

    PROCESS_TIMES m_Times;

    Status m_Status = Init;

    logger _L_;

public:
    WolfTask() = delete;

    WolfTask(logger pLog, const std::wstring& keyword)
        : m_cmdKeyword(keyword)
        , _L_(std::move(pLog))
    {
        ZeroMemory(&m_Times, sizeof(PROCESS_TIMES));
        ZeroMemory(&m_LastActiveTime, sizeof(FILETIME));
        ZeroMemory(&m_StartTime, sizeof(FILETIME));
    };

    HRESULT ApplyNotification(
        const DWORD dwLongerKeyword,
        const std::shared_ptr<CommandNotification>& notification,
        std::vector<std::shared_ptr<CommandMessage>>& actions);

    ~WolfTask(void);
};
}  // namespace Command::Wolf
}  // namespace Orc
#pragma managed(pop)
