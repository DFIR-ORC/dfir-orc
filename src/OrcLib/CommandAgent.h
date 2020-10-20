//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "Semaphore.h"

#include "CommandMessage.h"
#include "CommandNotification.h"
#include "ArchiveMessage.h"

#include "CommandAgentResources.h"
#include "JobObject.h"

#include <optional>
#include <vector>

#include <agents.h>

#include <concurrent_queue.h>

#pragma managed(push, off)

auto constexpr DEFAULT_MAX_RUNNING_PROCESSES = 20;

namespace Orc {

class ProcessRedirect;
class CommandExecute;

struct JOBOBJECT_CPU_RATE_CONTROL_INFORMATION
{
    DWORD ControlFlags;
    union
    {
        DWORD CpuRate;
        DWORD Weight;
        struct
        {
            WORD MinRate;
            WORD MaxRate;
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
};
using PJOBOBJECT_CPU_RATE_CONTROL_INFORMATION = JOBOBJECT_CPU_RATE_CONTROL_INFORMATION*;

constexpr auto JobObjectCpuRateControlInformation = (JOBOBJECTINFOCLASS)15;
#ifndef JOB_OBJECT_CPU_RATE_CONTROL_ENABLE
constexpr auto JOB_OBJECT_CPU_RATE_CONTROL_ENABLE = 0x1;
#endif
#ifndef JOB_OBJECT_CPU_RATE_CONTROL_WEIGHT_BASED
constexpr auto JOB_OBJECT_CPU_RATE_CONTROL_WEIGHT_BASED = 0x2;
#endif
#ifndef JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP
constexpr auto JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP = 0x4;
#endif

struct JobRestrictions
{
    JobRestrictions() = default;
    JobRestrictions(JobRestrictions&& other) noexcept = default;
    JobRestrictions(JobRestrictions& other) = default;
    JobRestrictions& operator=(const JobRestrictions&) = default;
    std::optional<JOBOBJECT_BASIC_UI_RESTRICTIONS> UiRestrictions;
    std::optional<JOBOBJECT_END_OF_JOB_TIME_INFORMATION> EndOfJob;
    std::optional<JOBOBJECT_EXTENDED_LIMIT_INFORMATION> ExtendedLimits;
    std::optional<JOBOBJECT_CPU_RATE_CONTROL_INFORMATION> CpuRateControl;
};

class CommandTerminationHandler;

class ORCLIB_API CommandAgent : public Concurrency::agent
{
public:
    CommandAgent(
        CommandMessage::ISource& source,
        ArchiveMessage::ITarget& archive,
        CommandNotification::ITarget& target,
        unsigned int max_running_tasks = DEFAULT_MAX_RUNNING_PROCESSES)
        : m_source(source)
        , m_target(target)
        , m_archive(archive)
        , m_MaximumRunningSemaphore(max_running_tasks)
        , m_Ressources()
    {
    }

    static HRESULT ApplyPattern(
        const std::wstring& Pattern,
        const std::wstring& KeyWord,
        const std::wstring& FileName,
        std::wstring& output);

    static HRESULT ReversePattern(
        const std::wstring& Pattern,
        const std::wstring& input,
        std::wstring& SystemType,
        std::wstring& FullComputerName,
        std::wstring& ComputerName,
        std::wstring& TimeStamp);

    HRESULT Initialize(
        const std::wstring& keyword,
        bool bChildDebug,
        const std::wstring& szTempDir,
        const JobRestrictions& JobRestrictions,
        CommandMessage::ITarget* pMessageTarget = nullptr);
    HRESULT UnInitialize();

    ~CommandAgent(void);

protected:
    Semaphore m_MaximumRunningSemaphore;

    JobObject m_Job;
    bool m_bLimitsMUSTApply = true;
    bool m_bWillRequireBreakAway = true;

    HANDLE m_hCompletionPort = INVALID_HANDLE_VALUE;

    JobRestrictions m_jobRestrictions;

    std::wstring m_TempDir;
    std::wstring m_Keyword;

    CommandAgentResources m_Ressources;

    bool m_bChildDebug = false;

    CommandNotification::ITarget& m_target;
    CommandMessage::ISource& m_source;

    ArchiveMessage::ITarget& m_archive;

    Concurrency::critical_section m_cs;

    Concurrency::concurrent_queue<std::shared_ptr<CommandExecute>> m_CommandQueue;
    std::vector<std::shared_ptr<CommandExecute>> m_RunningCommands;
    std::vector<std::shared_ptr<CommandExecute>> m_CompletedCommands;

    bool m_bStopping = false;
    std::shared_ptr<CommandTerminationHandler> m_pTerminationHandler;

    bool SendResult(const CommandNotification::Notification& request)
    {
        return Concurrency::send<CommandNotification::Notification>(m_target, request);
    }
    CommandMessage::Message GetRequest() { return Concurrency::receive<CommandMessage::Message>(m_source); }

    std::shared_ptr<ProcessRedirect>
    PrepareRedirection(const std::shared_ptr<CommandExecute>& cmd, const CommandParameter& output);
    std::shared_ptr<CommandExecute> PrepareCommandExecute(const std::shared_ptr<CommandMessage>& message);

    HRESULT ExecuteNextCommand();

    static DWORD WINAPI JobObjectNotificationRoutine(__in LPVOID lpParameter);
    static VOID CALLBACK WaitOrTimerCallbackFunction(__in PVOID lpParameter, __in BOOLEAN TimerOrWaitFired);

    HRESULT MoveCompletedCommand(const std::shared_ptr<CommandExecute>& command);

    virtual void run();
};

}  // namespace Orc

#pragma managed(pop)
