//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "CommandExecute.h"
#include "CommandNotification.h"
#include "CommandMessage.h"
#include "ProcessRedirect.h"
#include "CommandAgent.h"

#include "TemporaryStream.h"
#include "EmbeddedResource.h"

#include "Temporary.h"

#include "LogFileWriter.h"
#include "ParameterCheck.h"
#include "SystemDetails.h"

#include "Robustness.h"

#include <array>

using namespace std;

using namespace Orc;

class Orc::CommandTerminationHandler : public TerminationHandler
{
public:
    CommandTerminationHandler(const std::wstring& strDescr, CommandAgent* pAgent, CommandMessage::ITarget* pTarget)
        : TerminationHandler(strDescr, ROBUSTNESS_COMMAND)
        , m_pAgent(pAgent)
        , m_pMessageTarget(pTarget) {};

    HRESULT operator()();

private:
    CommandAgent* m_pAgent;
    CommandMessage::ITarget* m_pMessageTarget;
};

HRESULT CommandTerminationHandler::operator()()
{
    if (m_pMessageTarget)
    {
        auto killmessage = CommandMessage::MakeTerminateAllMessage();
        Concurrency::send(m_pMessageTarget, killmessage);

        auto donemessage = CommandMessage::MakeDoneMessage();
        Concurrency::send(m_pMessageTarget, donemessage);
    }

    if (m_pAgent)
        Concurrency::agent::wait(m_pAgent, 10000);

    return S_OK;
}

HRESULT CommandAgent::Initialize(
    const std::wstring& keyword,
    bool bChildDebug,
    const std::wstring& szTempDir,
    const JobRestrictions& jobRestrictions,
    CommandMessage::ITarget* pMessageTarget)
{
    m_Keyword = keyword;
    m_TempDir = szTempDir;

    m_Ressources.SetTempDirectory(m_TempDir);

    m_bChildDebug = bChildDebug;

    m_jobRestrictions = jobRestrictions;

    m_pTerminationHandler = std::make_shared<CommandTerminationHandler>(keyword, this, pMessageTarget);
    Robustness::AddTerminationHandler(m_pTerminationHandler);
    return S_OK;
}

HRESULT CommandAgent::UnInitialize()
{
    if (m_pTerminationHandler != nullptr)
        Robustness::RemoveTerminationHandler(m_pTerminationHandler);
    return S_OK;
}

std::shared_ptr<ProcessRedirect>
CommandAgent::PrepareRedirection(const shared_ptr<CommandExecute>& cmd, const CommandParameter& output)
{
    HRESULT hr = E_FAIL;
    DBG_UNREFERENCED_PARAMETER(cmd);

    std::shared_ptr<ProcessRedirect> retval;

    auto stream = std::make_shared<TemporaryStream>(_L_);

    if (FAILED(hr = stream->Open(m_TempDir, L"CommandRedirection", 4 * 1024 * 1024)))
    {
        log::Error(_L_, hr, L"Failed to create temporary stream\r\n");
        return nullptr;
    }

    switch (output.Kind)
    {
        case CommandParameter::StdOut:
            retval = ProcessRedirect::MakeRedirect(_L_, ProcessRedirect::StdOutput, stream, false);
            break;
        case CommandParameter::StdErr:
            retval = ProcessRedirect::MakeRedirect(_L_, ProcessRedirect::StdError, stream, false);
            break;
        case CommandParameter::StdOutErr:
            retval = ProcessRedirect::MakeRedirect(
                _L_,
                static_cast<ProcessRedirect::ProcessInOut>(ProcessRedirect::StdError | ProcessRedirect::StdOutput),
                stream,
                false);
            break;
        default:
            break;
    }

    if (retval)
    {
        HRESULT hr2 = E_FAIL;
        if (FAILED(hr2 = retval->CreatePipe(output.Keyword.c_str())))
        {
            log::Error(_L_, hr2, L"Could not create pipe for process redirection\r\n");
            return nullptr;
        }
    }

    return retval;
}

HRESULT CommandAgent::ApplyPattern(
    const std::wstring& Pattern,
    const std::wstring& KeyWord,
    const std::wstring& Name,
    std::wstring& output)
{
    static const std::wregex r_Name(L"\\{Name\\}");
    static const std::wregex r_FileName(L"\\{FileName\\}");
    static const std::wregex r_DirectoryName(L"\\{DirectoryName\\}");
    static const std::wregex r_FullComputerName(L"\\{FullComputerName\\}");
    static const std::wregex r_ComputerName(L"\\{ComputerName\\}");
    static const std::wregex r_TimeStamp(L"\\{TimeStamp\\}");
    static const std::wregex r_SystemType(L"\\{SystemType\\}");

    auto s0 = Pattern;

    wstring fmt_keyword = wstring(L"\"") + KeyWord + L"\"";
    auto s1 = std::regex_replace(s0, r_Name, fmt_keyword);

    wstring fmt_name = wstring(L"\"") + Name + L"\"";
    auto s2 = std::regex_replace(s1, r_FileName, fmt_name);
    auto s3 = std::regex_replace(s2, r_DirectoryName, fmt_name);

    wstring strFullComputerName;
    SystemDetails::GetOrcFullComputerName(strFullComputerName);
    auto s4 = std::regex_replace(s3, r_FullComputerName, strFullComputerName);

    wstring strComputerName;
    SystemDetails::GetOrcComputerName(strComputerName);
    auto s5 = std::regex_replace(s4, r_ComputerName, strComputerName);

    wstring strTimeStamp;
    SystemDetails::GetTimeStamp(strTimeStamp);
    auto s6 = std::regex_replace(s5, r_TimeStamp, strTimeStamp);

    wstring strSystemType;
    SystemDetails::GetSystemType(strSystemType);
    auto s7 = std::regex_replace(s6, r_SystemType, strSystemType);

    std::swap(s7, output);
    return S_OK;
}

HRESULT CommandAgent::ReversePattern(
    const std::wstring& Pattern,
    const std::wstring& input,
    std::wstring& SystemType,
    std::wstring& FullComputerName,
    std::wstring& ComputerName,
    std::wstring& TimeStamp)
{
    const std::wregex r_All(L"(\\{FullComputerName\\})|(\\{ComputerName\\})|(\\{TimeStamp\\})|(\\{SystemType\\})");
    const int FullComputerNameIdx = 1, ComputerNameIdx = 2, TimeStampIdx = 3, SystemTypeIdx = 4;

    array<int, 5> PatternIndex = {0, 0, 0, 0, 0};
    {
        auto it = std::wsregex_iterator(begin(Pattern), end(Pattern), r_All);
        std::wsregex_iterator rend;

        int Idx = 1;
        while (it != rend)
        {

            auto submatch = *it;

            if (submatch[SystemTypeIdx].matched)
                PatternIndex[SystemTypeIdx] = Idx;
            if (submatch[ComputerNameIdx].matched)
                PatternIndex[ComputerNameIdx] = Idx;
            if (submatch[FullComputerNameIdx].matched)
                PatternIndex[FullComputerNameIdx] = Idx;
            if (submatch[TimeStampIdx].matched)
                PatternIndex[TimeStampIdx] = Idx;

            ++it;
            Idx++;
        }
    }

    wstring strReverseRegEx = Pattern;

    if (PatternIndex[SystemTypeIdx] > 0)
    {
        const std::wregex r_SystemType(L"\\{SystemType\\}");
        strReverseRegEx = std::regex_replace(strReverseRegEx, r_SystemType, L"(WorkStation|DomainController|Server)");
    }
    if (PatternIndex[ComputerNameIdx] > 0)
    {
        const std::wregex r_ComputerName(L"\\{ComputerName\\}");
        strReverseRegEx = std::regex_replace(strReverseRegEx, r_ComputerName, L"(.*)");
    }
    if (PatternIndex[FullComputerNameIdx] > 0)
    {
        const std::wregex r_FullComputerName(L"\\{FullComputerName\\}");
        strReverseRegEx = std::regex_replace(strReverseRegEx, r_FullComputerName, L"(.*)");
    }
    if (PatternIndex[TimeStampIdx] > 0)
    {
        const std::wregex r_TimeStamp(L"\\{TimeStamp\\}");
        strReverseRegEx = std::regex_replace(strReverseRegEx, r_TimeStamp, L"([0-9]+)");
    }

    const wregex re(strReverseRegEx);

    wsmatch inputMatch;

    if (regex_match(input, inputMatch, re))
    {
        if (PatternIndex[SystemTypeIdx] > 0)
        {
            if (inputMatch[PatternIndex[SystemTypeIdx]].matched)
            {
                SystemType = inputMatch[PatternIndex[SystemTypeIdx]].str();
            }
        }
        if (PatternIndex[ComputerNameIdx] > 0)
        {
            if (inputMatch[PatternIndex[ComputerNameIdx]].matched)
            {
                ComputerName = inputMatch[PatternIndex[ComputerNameIdx]].str();
            }
        }
        if (PatternIndex[FullComputerNameIdx] > 0)
        {
            if (inputMatch[PatternIndex[FullComputerNameIdx]].matched)
            {
                FullComputerName = inputMatch[PatternIndex[FullComputerNameIdx]].str();
            }
        }
        if (PatternIndex[TimeStampIdx] > 0)
        {
            if (inputMatch[PatternIndex[TimeStampIdx]].matched)
            {
                TimeStamp = inputMatch[PatternIndex[TimeStampIdx]].str();
            }
        }
    }
    else
    {
        // pattern does not match
        return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
    }
    return S_OK;
}

std::shared_ptr<CommandExecute> CommandAgent::PrepareCommandExecute(const std::shared_ptr<CommandMessage>& message)
{
    auto retval = std::make_shared<CommandExecute>(_L_, message->Keyword());
    HRESULT hr = S_OK;

    std::for_each(
        message->GetParameters().cbegin(),
        message->GetParameters().cend(),
        [this, &hr, retval](const CommandParameter& parameter) {
            switch (parameter.Kind)
            {
                case CommandParameter::StdOut:
                case CommandParameter::StdErr:
                case CommandParameter::StdOutErr:
                {
                    wstring strFileName;

                    if (FAILED(hr = ApplyPattern(parameter.Keyword, L"", L"", strFileName)))
                    {
                        log::Error(
                            _L_, hr, L"Failed to apply parttern on output name '%s'\r\n", parameter.Name.c_str());
                        return;
                    }

                    auto redir = PrepareRedirection(retval, parameter);
                    if (!redir)
                        hr = E_FAIL;
                    else
                    {
                        retval->AddRedirection(redir);
                        retval->AddOnCompleteAction(make_shared<OnComplete>(
                            OnComplete::ArchiveAndDelete, strFileName, redir->GetStream(), &m_archive));
                    }
                }
                break;
                case CommandParameter::OutFile:
                {
                    wstring strInterpretedName;

                    if (FAILED(hr = GetOutputFile(parameter.Name.c_str(), strInterpretedName)))
                    {
                        log::Error(
                            _L_, hr, L"GetOutputFile failed, skipping file '%s'\r\n", (LPCWSTR)parameter.Name.c_str());
                        return;
                    }

                    wstring strFileName;

                    if (FAILED(hr = ApplyPattern(strInterpretedName, L"", L"", strFileName)))
                    {
                        log::Error(
                            _L_, hr, L"Failed to apply parttern on output name '%s'\r\n", parameter.Name.c_str());
                        return;
                    }

                    wstring strFilePath;

                    WCHAR szTempDir[MAX_PATH];
                    if (!m_TempDir.empty())
                        wcsncpy_s(szTempDir, m_TempDir.c_str(), m_TempDir.size());
                    else if (FAILED(hr = UtilGetTempDirPath(szTempDir, MAX_PATH)))
                        return;

                    if (FAILED(hr = UtilGetUniquePath(szTempDir, strFileName.c_str(), strFilePath)))
                    {
                        log::Error(_L_, hr, L"UtilGetUniquePath failed, skipping file '%s'\r\n", strFileName.c_str());
                        return;
                    }
                    else
                    {
                        // File does not exist prior to execution, we can delete when cabbed
                        retval->AddOnCompleteAction(make_shared<OnComplete>(
                            OnComplete::ArchiveAndDelete, strFileName, strFilePath, &m_archive));

                        wstring Arg;
                        if (FAILED(hr = ApplyPattern(parameter.Pattern, parameter.Keyword, strFilePath, Arg)))
                            return;
                        if (!Arg.empty())
                            retval->AddArgument(Arg, parameter.OrderId);
                    }
                }
                break;
                case CommandParameter::OutTempFile:
                {
                    // I don't really know if we need those...
                }
                break;
                case CommandParameter::OutDirectory:
                {
                    wstring strInterpretedName;

                    if (FAILED(hr = GetOutputFile(parameter.Name.c_str(), strInterpretedName)))
                    {
                        log::Error(
                            _L_,
                            hr,
                            L"GetOutputFile failed, skipping directory '%s'\r\n",
                            (LPCWSTR)parameter.Name.c_str());
                        return;
                    }

                    WCHAR szTempDir[MAX_PATH];
                    if (!m_TempDir.empty())
                        wcsncpy_s(szTempDir, m_TempDir.c_str(), m_TempDir.size());
                    else if (FAILED(hr = UtilGetTempDirPath(szTempDir, MAX_PATH)))
                        return;

                    wstring strDirName;

                    if (FAILED(hr = ApplyPattern(strInterpretedName, L"", L"", strDirName)))
                    {
                        log::Error(
                            _L_, hr, L"Failed to apply parttern on output name '%s'\r\n", parameter.Name.c_str());
                        return;
                    }

                    wstring strDirPath;
                    if (FAILED(hr = UtilGetUniquePath(szTempDir, strDirName.c_str(), strDirPath)))
                    {
                        log::Error(
                            _L_,
                            hr,
                            L"UtilGetUniquePath failed, skipping directory '%s': 0x%x\r\n",
                            strDirName.c_str());
                        return;
                    }

                    if (FAILED(VerifyDirectoryExists(strDirPath.c_str())))
                    {
                        // Directory does not exist prior to execution, create it, then we can delete when cabbed

                        if (!CreateDirectory(strDirPath.c_str(), NULL))
                        {
                            log::Error(
                                _L_,
                                hr = HRESULT_FROM_WIN32(GetLastError()),
                                L"could not create directory, skipping directory '%s'\r\n",
                                strDirPath.c_str());
                            return;
                        }
                    }

                    retval->AddOnCompleteAction(std::make_shared<OnComplete>(
                        OnComplete::ArchiveAndDelete, strDirName, strDirPath, parameter.MatchPattern, &m_archive));

                    wstring Arg;
                    if (FAILED(hr = ApplyPattern(parameter.Pattern, parameter.Keyword, strDirPath, Arg)))
                        return;
                    if (!Arg.empty())
                        retval->AddArgument(Arg, parameter.OrderId);
                }
                break;
                case CommandParameter::Argument:
                    if (FAILED(hr = retval->AddArgument(parameter.Keyword, parameter.OrderId)))
                        return;
                    break;
                case CommandParameter::Executable:
                {

                    if (EmbeddedResource::IsResourceBased(parameter.Name))
                    {
                        wstring extracted;

                        if (FAILED(hr = m_Ressources.GetResource(parameter.Name, parameter.Keyword, extracted)))
                        {
                            log::Error(_L_, hr, L"Failed to extract ressource %s\r\n", parameter.Name.c_str());
                            return;
                        }
                        if (FAILED(hr = retval->AddExecutableToRun(extracted)))
                            return;
                    }
                    else
                    {
                        if (FAILED(hr = retval->AddExecutableToRun(parameter.Name)))
                            return;
                    }
                }
                break;
                case CommandParameter::InFile:
                {
                    if (EmbeddedResource::IsResourceBased(parameter.Name))
                    {
                        wstring extracted;
                        if (FAILED(
                                hr = EmbeddedResource::ExtractToFile(
                                    _L_,
                                    parameter.Name,
                                    parameter.Keyword,
                                    RESSOURCE_GENERIC_READ_BA,
                                    m_TempDir,
                                    extracted)))
                        {
                            log::Error(_L_, hr, L"Failed to extract ressource %s from cab\r\n", parameter.Name.c_str());
                            return;
                        }
                        wstring Arg;
                        if (FAILED(hr = ApplyPattern(parameter.Pattern, parameter.Keyword, extracted, Arg)))
                            return;
                        if (!Arg.empty())
                            retval->AddArgument(Arg, parameter.OrderId);

                        if (FAILED(
                                hr = retval->AddOnCompleteAction(
                                    make_shared<OnComplete>(OnComplete::Delete, parameter.Name, extracted))))
                            return;
                    }
                    else
                    {
                        WCHAR inputfile[MAX_PATH];
                        if (FAILED(hr = GetInputFile(parameter.Name.c_str(), inputfile, MAX_PATH)))
                            return;

                        wstring Arg;
                        if (FAILED(hr = ApplyPattern(parameter.Pattern, parameter.Keyword, parameter.Name, Arg)))
                            return;
                        if (!Arg.empty())
                            retval->AddArgument(Arg, parameter.OrderId);
                        return;
                    }
                }
            }
        });

    switch (message->GetQueueBehavior())
    {
        case CommandMessage::Enqueue:
            break;
        case CommandMessage::FlushQueue:
            retval->AddOnCompleteAction(make_shared<OnComplete>(OnComplete::FlushArchiveQueue, &m_archive));
            break;
    }

    if (m_bChildDebug)
    {
        log::Verbose(_L_, L"CommandAgent: Configured dump file path %s\r\n", m_TempDir.c_str());
        retval->AddDumpFileDirectory(m_TempDir);
    }

    if (FAILED(hr))
        return nullptr;

    return retval;
}

typedef struct _CompletionBlock
{
    CommandAgent* pAgent;
    std::shared_ptr<CommandExecute> command;
} CompletionBlock;

VOID CALLBACK CommandAgent::WaitOrTimerCallbackFunction(__in PVOID lpParameter, __in BOOLEAN TimerOrWaitFired)
{
    DBG_UNREFERENCED_PARAMETER(TimerOrWaitFired);
    CompletionBlock* pBlock = (CompletionBlock*)lpParameter;

    pBlock->pAgent->m_MaximumRunningSemaphore.Release();

    auto retval = CommandNotification::NotifyProcessTerminated(
        pBlock->command->m_pi.dwProcessId, pBlock->command->GetKeyword(), pBlock->command->m_pi.hProcess);
    Concurrency::asend(pBlock->pAgent->m_target, retval);

    pBlock->command->SetStatus(CommandExecute::Complete);

    pBlock->pAgent->MoveCompletedCommand(pBlock->command);
    pBlock->command = nullptr;

    pBlock->pAgent->ExecuteNextCommand();
    pBlock->pAgent = nullptr;

    Concurrency::Free(pBlock);
    return;
}

HRESULT CommandAgent::MoveCompletedCommand(const std::shared_ptr<CommandExecute>& command)
{
    Concurrency::critical_section::scoped_lock s(m_cs);

    m_CompletedCommands.push_back(command);

    std::for_each(m_RunningCommands.begin(), m_RunningCommands.end(), [command](std::shared_ptr<CommandExecute>& item) {
        if (item)
            if (item.get() == command.get())
                item = nullptr;
    });

    return S_OK;
}

HRESULT CommandAgent::ExecuteNextCommand()
{
    HRESULT hr = E_FAIL;

    if (!m_MaximumRunningSemaphore.TryAcquire())
        return S_OK;

    std::shared_ptr<CommandExecute> command;

    {
        Concurrency::critical_section::scoped_lock lock(m_cs);

        if (!m_CommandQueue.try_pop(command))
        {
            // nothing queued, release the semaphore
            command = nullptr;
            m_MaximumRunningSemaphore.Release();
        }
    }
    if (command)
    {
        // there is something to execute in the queue

        hr = command->Execute(m_Job, m_bWillRequireBreakAway);

        if (SUCCEEDED(hr))
        {
            {
                Concurrency::critical_section::scoped_lock s(m_cs);
                m_RunningCommands.push_back(command);
            }

            HANDLE hWaitObject = INVALID_HANDLE_VALUE;

            CompletionBlock* pBlockPtr = (CompletionBlock*)Concurrency::Alloc(sizeof(CompletionBlock));

            CompletionBlock* pBlock = new (pBlockPtr) CompletionBlock;
            pBlock->pAgent = this;
            pBlock->command = command;

            if (!RegisterWaitForSingleObject(
                    &hWaitObject,
                    command->GetProcessHandle(),
                    WaitOrTimerCallbackFunction,
                    pBlock,
                    INFINITE,
                    WT_EXECUTEDEFAULT | WT_EXECUTEONLYONCE))
            {
                log::Error(
                    _L_,
                    hr = HRESULT_FROM_WIN32(GetLastError()),
                    L"Could not register for process \"%s\" termination\r\n",
                    command->GetKeyword().c_str());
                return hr;
            }

            auto notification =
                CommandNotification::NotifyStarted(command->ProcessID(), command->GetKeyword(), command->m_pi.hProcess);
            SendResult(notification);
        }
        else
        {
            // Process execution failed to start, releasing semaphone
            m_MaximumRunningSemaphore.Release();
            command->CompleteExecution();
        }
    }

    return S_OK;
}

DWORD WINAPI CommandAgent::JobObjectNotificationRoutine(__in LPVOID lpParameter)
{
    DWORD dwNbBytes = 0;
    CommandAgent* pAgent = nullptr;
    LPOVERLAPPED lpOverlapped = nullptr;

    if (!GetQueuedCompletionStatus((HANDLE)lpParameter, &dwNbBytes, (PULONG_PTR)&pAgent, &lpOverlapped, 1000))
    {
        DWORD err = GetLastError();
        if (err != ERROR_TIMEOUT && err != STATUS_TIMEOUT)
        {
            if (err != ERROR_INVALID_HANDLE && err != ERROR_ABANDONED_WAIT_0)
            {
                // Error invalid handle "somewhat" expected. Log the others
                logger pLog = std::make_shared<LogFileWriter>();
                log::Error(pLog, HRESULT_FROM_WIN32(err), L"GetQueuedCompletionStatus failed\r\n");
            }
            return 0;
        }
    }

    CommandNotification::Notification note;

    switch (dwNbBytes)
    {
        case JOB_OBJECT_MSG_END_OF_JOB_TIME:
            note = CommandNotification::NotifyJobTimeLimit();
            break;
        case JOB_OBJECT_MSG_END_OF_PROCESS_TIME:
            note = CommandNotification::NotifyProcessTimeLimit((DWORD_PTR)lpOverlapped);
            break;
        case JOB_OBJECT_MSG_ACTIVE_PROCESS_LIMIT:
            note = CommandNotification::NotifyJobProcessLimit();
            break;
        case JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO:
            note = CommandNotification::NotifyJobEmpty();
            break;
        case JOB_OBJECT_MSG_NEW_PROCESS:
            break;
        case JOB_OBJECT_MSG_EXIT_PROCESS:
            break;
        case JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS:
            note = CommandNotification::NotifyProcessAbnormalTermination((DWORD_PTR)lpOverlapped);
            break;
        case JOB_OBJECT_MSG_PROCESS_MEMORY_LIMIT:
            note = CommandNotification::NotifyProcessMemoryLimit((DWORD_PTR)lpOverlapped);
            break;
        case JOB_OBJECT_MSG_JOB_MEMORY_LIMIT:
            note = CommandNotification::NotifyJobMemoryLimit();
            break;
    }
    if (note)
    {
        if (pAgent != nullptr)
            pAgent->SendResult(note);
    }
    QueueUserWorkItem(JobObjectNotificationRoutine, lpParameter, WT_EXECUTEDEFAULT);
    return 0;
}

void CommandAgent::run()
{
    CommandMessage::Message request = GetRequest();

    CommandNotification::Notification notification;

    m_hCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, (ULONG_PTR)this, 0);
    if (m_hCompletionPort == NULL)
    {
        log::Error(
            _L_,
            HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to create IO Completion port for job object\r\n",
            m_Keyword.c_str());
        done();
        return;
    }

    HANDLE hJob = INVALID_HANDLE_VALUE;
    auto hr = JobObject::GetJobObject(_L_, GetCurrentProcess(), hJob);

    if (hr == S_OK && hJob != INVALID_HANDLE_VALUE)
    {
        JobObject currentJob = JobObject(_L_, hJob);
        if (currentJob.IsValid())
        {
            log::Verbose(_L_, L"WolfLauncher is running under an existing job!\r\n");
            if (currentJob.IsBreakAwayAllowed())
            {
                log::Verbose(_L_, L"Breakaway is allowed, we create our own job and use it!\r\n");
                m_Job = JobObject(_L_, m_Keyword.c_str());
                m_bWillRequireBreakAway = true;
            }
            else
            {
                // OK, tricky case here... we are in a job, that does not allow breakaway...
                log::Verbose(_L_, L"Breakaway is not allowed!\r\n");

                // as we'll be hosted inside a "foreign" job or a nested job, limits may or may not apply
                m_bLimitsMUSTApply = false;
                m_bWillRequireBreakAway = false;

                auto [major, minor] = SystemDetails::GetOSVersion();
                if (major >= 6 && minor >= 2)
                {
                    log::Verbose(
                        _L_, L"Current Windows version allows nested jobs. We create our own job and use it!\r\n");
                    m_Job = JobObject(_L_, m_Keyword.c_str());
                }
                else
                {
                    log::Verbose(
                        _L_,
                        L"Current Windows version does not allow nested jobs. We have to use the current job!\r\n");
                    m_Job = std::move(currentJob);
                }
            }
        }
    }
    else if (hr == S_OK && hJob == INVALID_HANDLE_VALUE)
    {
        log::Verbose(_L_, L"WolfLauncher is not running under any job!\r\n");
        m_Job = JobObject(_L_, m_Keyword.c_str());
    }
    else
    {
        log::Verbose(_L_, L"WolfLauncher is running under a job we could not determine!\r\n");
        JobObject currentJob = JobObject(_L_, (HANDLE)NULL);
        if (currentJob.IsValid())
        {
            log::Verbose(_L_, L"WolfLauncher is running under an existing job!\r\n");
            if (currentJob.IsBreakAwayAllowed())
            {
                log::Verbose(_L_, L"Breakaway is allowed, we create our own job and use it!\r\n");
                m_Job = JobObject(_L_, m_Keyword.c_str());
                if (!m_Job.IsValid())
                {
                    log::Error(_L_, E_FAIL, L"Failed to create job\r\n");
                }
                m_bWillRequireBreakAway = true;
            }
            else
            {
                // OK, tricky case here... we are in a job, don't know which one, that does not allow breakaway...
                log::Verbose(_L_, L"Breakaway is not allowed!\r\n");

                // as we'll be hosted inside a "foreign" job or a nested job, limits may or may not apply
                m_bLimitsMUSTApply = false;
                m_bWillRequireBreakAway = false;

                auto [major, minor] = SystemDetails::GetOSVersion();
                if (major >= 6 && minor >= 2)
                {
                    log::Verbose(
                        _L_, L"Current Windows version allows nested jobs. We create our own job and use it!\r\n");
                    m_Job = JobObject(_L_, m_Keyword.c_str());
                }
                else
                {
                    log::Error(
                        _L_,
                        E_FAIL,
                        L"Current Windows version does not allow nested jobs, we cannot determine which job, we cannot "
                        L"break away. We have to fail!\r\n");
                    done();
                    return;
                }
            }
        }
    }

    if (!m_Job.IsValid())
    {
        log::Error(_L_, HRESULT_FROM_WIN32(GetLastError()), L"Failed to create %s job object\r\n", m_Keyword.c_str());
        done();
        return;
    }

    if (FAILED(hr = m_Job.AssociateCompletionPort(m_hCompletionPort, this)))
    {
        log::Error(_L_, hr, L"Failed to associate job object with completion port (%s)\r\n", m_Keyword.c_str());
        done();
        return;
    }

    if (m_jobRestrictions.UiRestrictions)
    {
        if (!SetInformationJobObject(
                m_Job.GetHandle(),
                JobObjectBasicUIRestrictions,
                &m_jobRestrictions.UiRestrictions.value(),
                sizeof(JOBOBJECT_BASIC_UI_RESTRICTIONS)))
        {
            if (m_bLimitsMUSTApply)
            {
                log::Error(
                    _L_,
                    hr = HRESULT_FROM_WIN32(GetLastError()),
                    L"Failed to set basic UI restrictions on job object\r\n");
                done();
                return;
            }
            else
            {
                log::Warning(
                    _L_,
                    hr = HRESULT_FROM_WIN32(GetLastError()),
                    L"Failed to set basic UI restrictions on job object\r\n");
            }
        }
    }

    if (!m_jobRestrictions.EndOfJob)
    {
        m_jobRestrictions.EndOfJob.emplace();
        if (!QueryInformationJobObject(
                m_Job.GetHandle(),
                JobObjectEndOfJobTimeInformation,
                &m_jobRestrictions.EndOfJob.value(),
                sizeof(JOBOBJECT_END_OF_JOB_TIME_INFORMATION),
                NULL))
        {
            log::Error(
                _L_, HRESULT_FROM_WIN32(GetLastError()), L"Failed to retrieve end of job behavior on job object\r\n");
            done();
            return;
        }
    }

    if (m_jobRestrictions.EndOfJob.value().EndOfJobTimeAction != JOB_OBJECT_POST_AT_END_OF_JOB)
    {
        JOBOBJECT_END_OF_JOB_TIME_INFORMATION joeojti;
        joeojti.EndOfJobTimeAction = JOB_OBJECT_POST_AT_END_OF_JOB;

        // Tell the job object what we want it to do when the
        // job time is exceeded
        if (!SetInformationJobObject(m_Job.GetHandle(), JobObjectEndOfJobTimeInformation, &joeojti, sizeof(joeojti)))
        {
            log::Warning(
                _L_,
                HRESULT_FROM_WIN32(GetLastError()),
                L"Failed to set end of job behavior (to post at end of job) on job object\r\n");
        }
    }

    if (m_jobRestrictions.CpuRateControl)
    {
        auto [major, minor] = SystemDetails::GetOSVersion();

        auto bCpuRateControlFeaturePresent = false;
        if (major == 6 && minor >= 2)
            bCpuRateControlFeaturePresent = true;
        else if (major >= 10)
            bCpuRateControlFeaturePresent = true;

        if (bCpuRateControlFeaturePresent)
        {
            if (!SetInformationJobObject(
                    m_Job.GetHandle(),
                    JobObjectCpuRateControlInformation,
                    &m_jobRestrictions.CpuRateControl.value(),
                    sizeof(JOBOBJECT_CPU_RATE_CONTROL_INFORMATION)))
            {
                if (m_bLimitsMUSTApply)
                {
                    log::Error(
                        _L_,
                        hr = HRESULT_FROM_WIN32(GetLastError()),
                        L"Failed to set CPU Rate limits on job object\r\n");
                    done();
                    return;
                }
                else
                {
                    log::Warning(
                        _L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to CPU Rate limits on job object\r\n");
                }
            }
            else
            {
                log::Verbose(_L_, L"CPU rate limits successfully applied\r\n");
            }
        }
        else
        {
            log::Warning(
                _L_,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"CPU Rate limit: this Windows version does not support this feature (<6.2)\r\n");
        }
    }

    if (m_jobRestrictions.ExtendedLimits)
    {
        if (!SetInformationJobObject(
                m_Job.GetHandle(),
                JobObjectExtendedLimitInformation,
                &m_jobRestrictions.ExtendedLimits.value(),
                sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)))
        {
            if (m_bLimitsMUSTApply)
            {
                log::Error(
                    _L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to set extended limits on job object\r\n");
                done();
                return;
            }
            else
            {
                log::Warning(
                    _L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to set extended limits on job object\r\n");
            }
        }
    }

    // Make sure we have the limit JOB_OBJECT_LIMIT_BREAKAWAY_OK set (because we subsequently use the CreateProcess with
    // CREATE_BREAKAWAY_FROM_JOB
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION JobExtendedLimit;
    ZeroMemory(&JobExtendedLimit, sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
    DWORD returnedLength = 0;

    if (!QueryInformationJobObject(
            m_Job.GetHandle(),
            JobObjectExtendedLimitInformation,
            &JobExtendedLimit,
            sizeof(JobExtendedLimit),
            &returnedLength))
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to obtain extended limits on job object\r\n");
        done();
        return;
    }

    bool bRequiredChange = false;
    if (m_bWillRequireBreakAway && !(JobExtendedLimit.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_BREAKAWAY_OK))
    {
        // we are using a job we did not create
        JobExtendedLimit.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_BREAKAWAY_OK;
        bRequiredChange = true;
    }

    if (!(JobExtendedLimit.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION))
    {
        // Make sure we don't have annoying watson boxes...
        JobExtendedLimit.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
        bRequiredChange = true;
    }

    if (bRequiredChange)
    {
        if (!SetInformationJobObject(
                m_Job.GetHandle(),
                JobObjectExtendedLimitInformation,
                &JobExtendedLimit,
                sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)))
        {
            log::Error(
                _L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to set extended limits on job object\r\n");
            done();
            return;
        }
    }

    // We initiate the "listening" of the completion port for notifications
    if (!QueueUserWorkItem(JobObjectNotificationRoutine, m_hCompletionPort, WT_EXECUTEDEFAULT))
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to queue user work item for completion port\r\n",
            m_Keyword.c_str());
        done();
        return;
    }

    while (request)
    {
        switch (request->Request())
        {
            case CommandMessage::Execute:
            {
                log::Verbose(_L_, L"CommandAgent: Execute command %s\r\n", request->Keyword().c_str());
                if (!m_bStopping)
                {
                    auto command = PrepareCommandExecute(request);

                    m_CommandQueue.push(command);
                }
                else
                {
                    log::Error(
                        _L_, E_FAIL, L"Command %s rejected, command agent is stopping\r\n", request->Keyword().c_str());
                }
            }
            break;
            case CommandMessage::RefreshRunningList:
            {
                log::Verbose(_L_, L"CommandAgent: Refreshing running command list\r\n");

                Concurrency::critical_section::scoped_lock s(m_cs);
                auto new_end = std::remove_if(
                    m_RunningCommands.begin(),
                    m_RunningCommands.end(),
                    [this](const std::shared_ptr<CommandExecute>& item) -> bool {
                        if (item == nullptr)
                            return true;
                        if (item->HasCompleted())
                        {
                            item->CompleteExecution(&m_archive);
                            return true;
                        }
                        return false;
                    });
                m_RunningCommands.erase(new_end, m_RunningCommands.end());

                std::for_each(
                    m_RunningCommands.cbegin(),
                    m_RunningCommands.cend(),
                    [this](const std::shared_ptr<CommandExecute>& item) {
                        SendResult(CommandNotification::NotifyRunningProcess(item->GetKeyword(), item->ProcessID()));
                    });

                std::for_each(
                    m_CompletedCommands.cbegin(),
                    m_CompletedCommands.cend(),
                    [this](const std::shared_ptr<CommandExecute>& item) {
                        if (item)
                            if (item->HasCompleted())
                            {
                                item->CompleteExecution(&m_archive);
                            }
                    });
            }
            break;
            case CommandMessage::Terminate:
            {
                HANDLE hProcess =
                    OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION, FALSE, request->ProcessID());

                if (hProcess == NULL)
                {
                    SendResult(CommandNotification::NotifyFailure(
                        CommandNotification::Terminated,
                        HRESULT_FROM_WIN32(GetLastError()),
                        request->ProcessID(),
                        request->Keyword()));
                }
                else
                {
                    if (!TerminateProcess(hProcess, ERROR_PROCESS_ABORTED))
                    {
                        SendResult(CommandNotification::NotifyFailure(
                            CommandNotification::Terminated,
                            HRESULT_FROM_WIN32(GetLastError()),
                            request->ProcessID(),
                            request->Keyword()));
                    }
                    CloseHandle(hProcess);
                }
            }
            break;
            case CommandMessage::TerminateAll:
            {
                log::Verbose(_L_, L"CommandAgent: Terminate (kill) all running tasks\r\n");
                m_bStopping = true;
                {
                    Concurrency::critical_section::scoped_lock lock(m_cs);

                    std::shared_ptr<CommandExecute> cmd;
                    while (m_CommandQueue.try_pop(cmd))
                    {
                    }
                }
                if (!TerminateJobObject(m_Job.GetHandle(), (UINT)-1))
                {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    log::Error(_L_, hr, L"Failed to terminate job object %s\r\n", request->Keyword().c_str());
                }
                SendResult(CommandNotification::NotifyTerminateAll());
            }
            break;
            case CommandMessage::CancelAnyPendingAndStop:
            {
                log::Verbose(
                    _L_, L"CommandAgent: Cancel any pending task (ie not running) and stop accepting new ones\r\n");
                m_bStopping = true;
                {
                    Concurrency::critical_section::scoped_lock lock(m_cs);
                    std::shared_ptr<CommandExecute> cmd;
                    while (m_CommandQueue.try_pop(cmd))
                    {
                        log::Info(_L_, L"Canceling command %s\r\n", cmd->GetKeyword().c_str());
                    }
                }
                SendResult(CommandNotification::NotifyCanceled());
            }
            break;
            case CommandMessage::QueryRunningList:
            {
                log::Verbose(_L_, L"CommandAgent: Query running command list\r\n");

                Concurrency::critical_section::scoped_lock lock(m_cs);

                std::for_each(
                    m_RunningCommands.cbegin(),
                    m_RunningCommands.cend(),
                    [this](const std::shared_ptr<CommandExecute>& item) {
                        SendResult(CommandNotification::NotifyRunningProcess(item->GetKeyword(), item->ProcessID()));
                    });
            }
            break;
            case CommandMessage::Done:
            {
                log::Verbose(_L_, L"CommandAgent: Done message received\r\n");
                m_bStopping = true;
            }
            break;
            default:
                break;
        }

        if (FAILED(hr = ExecuteNextCommand()))
        {
            log::Error(_L_, hr, L"Failed to execute next command\r\n");
        }

        if (m_bStopping && m_RunningCommands.size() == 0 && m_CommandQueue.empty())
        {
            // delete temporary ressources
            m_Ressources.DeleteTemporaryRessources();
            SendResult(CommandNotification::NotifyDone(m_Keyword, m_Job.GetHandle()));
            done();

            m_Job.Close();
            CloseHandle(m_hCompletionPort);
            m_hCompletionPort = INVALID_HANDLE_VALUE;
            return;
        }
        else
        {
            request = GetRequest();
        }
    }

    return;
}

CommandAgent::~CommandAgent(void)
{
    if (m_Job.IsValid())
        CloseHandle(m_Job.GetHandle());
    if (m_hCompletionPort != INVALID_HANDLE_VALUE)
        CloseHandle(m_hCompletionPort);
    m_hCompletionPort = INVALID_HANDLE_VALUE;
}
