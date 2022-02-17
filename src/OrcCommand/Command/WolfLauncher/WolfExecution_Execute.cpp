//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <string>

#include <agents.h>

#include "WolfExecution.h"

#include "TableOutput.h"

#include "FileStream.h"
#include "TeeStream.h"
#include "TemporaryStream.h"
#include "JournalingStream.h"
#include "AccumulatingStream.h"

#include "ArchiveAgent.h"
#include "ArchiveMessage.h"
#include "ArchiveNotification.h"

#include "CommandAgent.h"
#include "CommandMessage.h"
#include "CommandNotification.h"

#include "ParameterCheck.h"
#include "Temporary.h"
#include "EmbeddedResource.h"
#include "SystemDetails.h"

#include "EncodeMessageStream.h"

#include "WolfTask.h"
#include "Convert.h"
#include "Utils/Time.h"
#include "Utils/WinApi.h"
#include "Text/HexDump.h"

using namespace Orc;
using namespace Orc::Command::Wolf;

namespace {

Outcome::Command::Output::Type ToOutputFileType(CommandParameter::ParamKind kind)
{
    using Type = Outcome::Command::Output::Type;
    using ParamKind = CommandParameter::ParamKind;

    switch (kind)
    {
        case ParamKind::StdOut:
            return Type::StdOut;
        case ParamKind::StdErr:
            return Type::StdErr;
        case ParamKind::StdOutErr:
            return Type::StdOutErr;
        case ParamKind::OutFile:
            return Type::File;
        case ParamKind::OutDirectory:
            return Type::Directory;
        default:
            return Type::Undefined;
    }
}

bool HasFileOutput(Orc::CommandParameter::ParamKind kind)
{
    using namespace Orc;

    switch (kind)
    {
        case Orc::CommandParameter::ParamKind::StdOut:
        case Orc::CommandParameter::ParamKind::StdErr:
        case Orc::CommandParameter::ParamKind::StdOutErr:
        case Orc::CommandParameter::ParamKind::OutFile:
        case Orc::CommandParameter::ParamKind::OutDirectory:
            return true;
        default:
            break;
    }

    return false;
}

Outcome::Archive::InputType GetArchiveInputType()
{
    const auto kOfflineLocation = L"%OFFLINELOCATION%";

    std::error_code ec;
    auto offlineLocation = Orc::ExpandEnvironmentStringsApi(kOfflineLocation, 512, ec);
    if (ec)
    {
        Log::Debug(L"Failed to expand environment variable {} [{}]", kOfflineLocation, ec);
        return Outcome::Archive::InputType::kUndefined;
    }

    if (offlineLocation.empty() || offlineLocation == kOfflineLocation)
    {
        return Outcome::Archive::InputType::kRunningSystem;
    }

    return Outcome::Archive::InputType::kOffline;
}

}

std::wstring WolfExecution::ToString(WolfExecution::Repeat value)
{
    switch (value)
    {
        case WolfExecution::Repeat::CreateNew:
            return L"CreateNew (always create new output files)";
        case WolfExecution::Repeat::Once:
            return L"Once (skip commands set if output file exist)";
        case WolfExecution::Repeat::Overwrite:
            return L"Overwrite (overwrite any existing output files)";
        case WolfExecution::Repeat::NotSet:
            return L"<default>";
        default:
            return Orc::kFailedConversionW;
    }
}

HRESULT WolfExecution::SetRecipients(const std::vector<std::shared_ptr<WolfExecution::Recipient>> recipients)
{
    for (const auto& recipient : recipients)
    {
        for (const auto& strArchiveSpec : recipient->ArchiveSpec)
        {
            if (PathMatchSpec(m_commandSet.c_str(), strArchiveSpec.c_str()))
            {
                m_Recipients.push_back(recipient);
            }
            else if (PathMatchSpec(m_strArchiveName.c_str(), strArchiveSpec.c_str()))
            {
                m_Recipients.push_back(recipient);
            }
        }
    }

    return S_OK;
}

HRESULT WolfExecution::BuildFullArchiveName()
{
    HRESULT hr = E_FAIL;

    if (m_strArchiveName.empty())
    {
        Log::Error("No valid archive file name specified, archive agent is not created");
        return E_FAIL;
    }
    else
    {
        Log::Debug(L"Archive file name specified is '{}'", m_strArchiveName);
    }

    if (IsPathComplete(m_strArchiveName.c_str()) == S_FALSE)
    {
        // A simple file name is used

        if (FAILED(hr = CommandAgent::ApplyPattern(m_strArchiveName, L"", L"", m_strArchiveFileName)))
            return hr;

        if (m_Output.Type != OutputSpec::Kind::None && m_RepeatBehavior == Repeat::CreateNew)
        {
            // an output directory is specified, create a new file here "."
            if (FAILED(
                    hr = UtilGetUniquePath(m_Output.Path.c_str(), m_strArchiveFileName.c_str(), m_strArchiveFullPath)))
                return hr;
        }
        else if (m_Output.Type != OutputSpec::Kind::None)
        {
            // simply create file in existing directory
            std::wstring strAchiveName;
            if (FAILED(hr = UtilGetPath(m_Output.Path.c_str(), m_strArchiveFileName.c_str(), strAchiveName)))
                return hr;

            if (FAILED(hr = GetOutputFile(strAchiveName.c_str(), m_strArchiveFullPath, true)))
                return hr;
        }
        else
        {
            Log::Error("No valid output directory specified");
            return E_INVALIDARG;
        }
    }
    else
    {
        // A full path was specified, using it
        m_strArchiveFullPath = m_strArchiveName;
        WCHAR szFileName[MAX_PATH];

        if (FAILED(hr = GetFileNameForFile(m_strArchiveFullPath.c_str(), szFileName, MAX_PATH)))
        {
            Log::Error("Unable to extract archive file name [{}]", SystemError(hr));
            return hr;
        }

        m_strArchiveFileName = szFileName;
    }

    if (!m_Recipients.empty())
    {
        m_strOutputFullPath = m_strArchiveFullPath + L".p7b";
        m_strOutputFileName = m_strArchiveFileName + L".p7b";
    }
    else
    {
        m_strOutputFullPath = m_strArchiveFullPath;
        m_strOutputFileName = m_strArchiveFileName;
    }

    return S_OK;
}

void WolfExecution::ArchiveNotificationHandler(const ArchiveNotification::Notification& notification)
{
    const std::wstring operation = L"Archive";

    HRESULT hr = notification->GetHResult();
    if (FAILED(hr))
    {
        const auto error = SystemError(notification->GetHResult());
        Log::Error(
            L"Failed creating archive '{}': {} [{}]", notification->Keyword(), notification->Description(), error);
        m_journal.Print(
            notification->Keyword(), operation, L"Archive failed '{}' [{}]", notification->GetFileName(), error);
        return;
    }

    auto&& lock = m_outcome.Lock();
    auto& outcomeArchive = m_outcome.GetCommandSet(m_commandSet).GetArchive();

    switch (notification->GetType())
    {
        case ArchiveNotification::ArchiveStarted: {
            m_journal.Print(m_commandSet, operation, L"Started");
            auto output = std::filesystem::path(notification->GetFileName());
            outcomeArchive.SetName(output.filename());
            break;
        }
        case ArchiveNotification::FileAddition: {
            m_journal.Print(
                m_commandSet, operation, L"Add file: {} ({})", notification->Keyword(), notification->FileSize());

            Outcome::Archive::Item item;
            item.SetName(notification->Keyword());
            item.SetSize(notification->FileSize());
            outcomeArchive.Add(item);
            break;
        }
        case ArchiveNotification::DirectoryAddition: {
            m_journal.Print(m_commandSet, operation, L"Add directory: {}", notification->Keyword());
            // Do not update Outcome as there will be 'ArchiveNotification::FileAddition' for them
            break;
        }
        case ArchiveNotification::StreamAddition: {
            m_journal.Print(m_commandSet, operation, L"Add stream: {}", notification->Keyword());

            Outcome::Archive::Item item;
            item.SetName(notification->Keyword());
            item.SetSize(notification->FileSize());
            outcomeArchive.Add(item);
            break;
        }
        case ArchiveNotification::ArchiveComplete: {
            m_journal.Print(
                m_commandSet, operation, L"Completed: {} ({})", notification->Keyword(), notification->FileSize());

            outcomeArchive.SetSize(notification->FileSize());

            if (m_archiveHashStream)
            {
                CBinaryBuffer buffer;
                hr = m_archiveHashStream->GetSHA1(buffer);
                if (FAILED(hr))
                {
                    Log::Error(L"Failed to retrieve SHA1 for '{}' [{}]", SystemError(hr));
                    hr = S_OK;
                }
                else if (!buffer.empty())
                {
                    std::string sha1;
                    std::string_view bufferView(buffer);
                    Text::ToHex(std::cbegin(bufferView), std::cend(bufferView), std::back_inserter(sha1));
                    outcomeArchive.SetSha1(sha1);
                }
            }
            else
            {
                auto sha1 = Hash(m_strOutputFullPath, CryptoHashStreamAlgorithm::SHA1);
                if (!sha1)
                {
                    Log::Error(L"Failed to retrieve SHA1 for '{}' [{}]", sha1.error());
                }

                outcomeArchive.SetSha1(Utf16ToUtf8(*sha1, "<encoding_error>"));
            }

            outcomeArchive.SetInputType(::GetArchiveInputType());
            break;
        }
    }
}

HRESULT WolfExecution::CreateArchiveAgent()
{
    HRESULT hr = E_FAIL;

    m_archiveNotification = std::make_unique<Concurrency::call<ArchiveNotification::Notification>>(
        [this](const ArchiveNotification::Notification& notification) { ArchiveNotificationHandler(notification); });

    m_archiveAgent =
        std::make_unique<ArchiveAgent>(m_ArchiveMessageBuffer, m_ArchiveMessageBuffer, *m_archiveNotification);
    if (!m_archiveAgent->start())
    {
        Log::Error("Start for archive Agent failed");
        return E_FAIL;
    }

    if (!m_Recipients.empty())
    {
        if (m_strOutputFullPath.empty())
        {
            Log::Error("Invalid empty output file name");
            return E_FAIL;
        }

        auto pOutputStream = std::make_shared<FileStream>();
        if (FAILED(hr = pOutputStream->WriteTo(m_strOutputFullPath.c_str())))
        {
            Log::Error(L"Failed to open file for write: '{}' [{}]", m_strOutputFullPath, SystemError(hr));
            return hr;
        }

        m_archiveHashStream = std::make_shared<CryptoHashStream>();
        hr = m_archiveHashStream->OpenToWrite(CryptoHashStream::Algorithm::SHA1, pOutputStream);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to open hash stream for write [{}]", SystemError(hr));
            return hr;
        }

        auto pEncodingStream = std::make_shared<EncodeMessageStream>();

        for (auto& recipient : m_Recipients)
        {
            if (FAILED(hr = pEncodingStream->AddRecipient(recipient->Certificate)))
            {
                Log::Error(L"Failed to add certificate for recipient '{}' [{}]", recipient->Name, SystemError(hr));
                return hr;
            }
        }
        if (FAILED(hr = pEncodingStream->Initialize(m_archiveHashStream)))
        {
            Log::Error(L"Failed initialize encoding stream for '{}' [{}]", m_strOutputFullPath, SystemError(hr));
            return hr;
        }

        std::shared_ptr<ByteStream> pFinalStream;

        if (UseJournalWhenEncrypting())
        {
            auto pJournalingStream = std::make_shared<JournalingStream>();

            if (FAILED(hr = pJournalingStream->Open(pEncodingStream)))
            {
                Log::Error(L"Failed open journaling stream to write [{}]", SystemError(hr));
                return hr;
            }
            pFinalStream = pJournalingStream;
        }
        else
        {
            auto pAccumulatingStream = std::make_shared<AccumulatingStream>();

            if (FAILED(hr = pAccumulatingStream->Open(pEncodingStream, m_Temporary.Path, 100 * 1024 * 1024)))
            {
                Log::Error(L"Failed open accumulating stream to write [{}]", SystemError(hr));
                return hr;
            }
            pFinalStream = pAccumulatingStream;
        }

        if (TeeClearTextOutput())
        {
            auto pClearStream = std::make_shared<FileStream>();

            if (FAILED(hr = pClearStream->WriteTo(m_strArchiveFullPath.c_str())))
            {
                Log::Error(L"Failed initialize file stream for '{}' [{}]", m_strArchiveFullPath, SystemError(hr));
                return hr;
            }

            auto pTeeTream = std::make_shared<TeeStream>();

            if (FAILED(hr = pTeeTream->Open({pClearStream, pFinalStream})))
            {
                Log::Error(
                    L"Failed initialize tee stream for '{}' & '{}' [{}]",
                    m_strOutputFileName,
                    m_strArchiveFullPath,
                    SystemError(hr));
                return hr;
            }

            pFinalStream = pTeeTream;
        }

        ArchiveFormat fmt = OrcArchive::GetArchiveFormat(m_strArchiveFileName);

        auto request = ArchiveMessage::MakeOpenRequest(m_strOutputFileName, fmt, pFinalStream, m_strCompressionLevel);
        Concurrency::send(m_ArchiveMessageBuffer, request);
    }
    else
    {
        ArchiveFormat fmt = OrcArchive::GetArchiveFormat(m_strArchiveFileName);

        auto request = ArchiveMessage::MakeOpenRequest(m_strOutputFullPath, fmt, nullptr, m_strCompressionLevel);
        Concurrency::send(m_ArchiveMessageBuffer, request);
    }

    return S_OK;
}

HRESULT WolfExecution::NotifyTask(const CommandNotification::Ptr& item)
{
    HRESULT hr = E_FAIL;

    std::shared_ptr<WolfTask> task;

    {
        auto taskiter = m_TasksByPID.find(static_cast<DWORD>(item->GetProcessID()));

        if (taskiter != m_TasksByPID.end())
        {
            task = taskiter->second;
        }
    }

    if (task == nullptr)
    {
        auto taskiter = m_TasksByKeyword.find(item->GetKeyword());

        if (taskiter != m_TasksByKeyword.end())
        {
            task = taskiter->second;
        }
    }

    if (task != nullptr)
    {
        std::vector<CommandMessage::Message> actions;
        task->ApplyNotification(item, actions);

        for (const auto& item : actions)
        {
            Concurrency::send(m_cmdAgentBuffer, item);
        }
    }
    return S_OK;
}

HRESULT WolfExecution::CreateCommandAgent(
    boost::tribool bGlobalChildDebug,
    std::chrono::milliseconds msRefresh,
    DWORD dwMaxTasks)
{
    using namespace std::literals;

    HRESULT hr = E_FAIL;

    m_pTermination = std::make_shared<WOLFExecutionTerminate>(m_commandSet, this);
    Robustness::AddTerminationHandler(m_pTermination);

    m_cmdNotification =
        std::make_unique<Concurrency::call<CommandNotification::Ptr>>([this](const CommandNotification::Ptr& item) {
            HRESULT hr = E_FAIL;

            if (item)
            {
                switch (item->GetEvent())
                {
                    case CommandNotification::Started: {
                        auto taskiter = m_TasksByKeyword.find(item->GetKeyword());
                        if (taskiter != m_TasksByKeyword.end())
                            m_TasksByPID[static_cast<DWORD>(item->GetProcessID())] = taskiter->second;
                        else
                        {
                            Log::Error(L"New task '{}' could not be found", item->GetKeyword());
                        }
                    }
                    break;
                    case CommandNotification::Terminated:
                        break;
                    case CommandNotification::Running:
                        break;
                    case CommandNotification::ProcessAbnormalTermination:
                        break;
                    case CommandNotification::ProcessMemoryLimit:
                        break;
                    case CommandNotification::ProcessTimeLimit:
                        break;
                    case CommandNotification::JobEmpty:
                        Log::Debug("No tasks are currently running");
                        break;
                    case CommandNotification::JobProcessLimit:
                        Log::Warn("JOB: Process number limit");
                        break;
                    case CommandNotification::JobMemoryLimit:
                        Log::Warn(L"JOB: Memory limit");
                        break;
                    case CommandNotification::JobTimeLimit:
                        Log::Warn(L"JOB: CPU Time limit");
                        break;
                    case CommandNotification::AllTerminated:
                        Log::Warn("JOB: Job was autoritatively terminated");
                        break;
                    case CommandNotification::Done: {
                        GetSystemTimeAsFileTime(&m_FinishTime);
                        {
                            auto start = Orc::ConvertTo(m_StartTime);
                            auto end = Orc::ConvertTo(m_FinishTime);
                            auto duration = end - start;

                            Log::Info(
                                L"{}: Complete! (commands took {} seconds)",
                                item->GetKeyword(),
                                duration.count() / 10000000);
                        }

                        Log::Info("JOB: Complete");

                        auto&& lock = m_outcome.Lock();

                        auto& commandSetOutcome = m_outcome.GetCommandSet(item->GetKeyword());
                        commandSetOutcome.SetKeyword(item->GetKeyword());
                        commandSetOutcome.SetStart(FromFileTime(m_StartTime));
                        commandSetOutcome.SetEnd(FromFileTime(m_FinishTime));

                        const auto jobStats = item->GetJobStatictics();
                        if (jobStats)
                        {
                            Outcome::JobStatistics statistics;

                            statistics.SetPageFaultCount(jobStats->TotalPageFaultCount);
                            statistics.SetProcessCount(jobStats->TotalProcesses);
                            statistics.SetActiveProcessCount(jobStats->ActiveProcesses);
                            statistics.SetTerminatedProcessCount(jobStats->TotalTerminatedProcesses);
                            statistics.SetPeakProcessMemory(jobStats->PeakProcessMemoryUsed);
                            statistics.SetPeakJobMemory(jobStats->PeakJobMemoryUsed);
                            statistics.SetIOCounters(jobStats->IoInfo);

                            commandSetOutcome.SetJobStatistics(statistics);
                        }

                        for (const auto& [keyword, task] : m_TasksByPID)
                        {
                            if (task == nullptr)
                            {
                                Log::Critical("Invalid task");
                                continue;
                            }

                            {
                                auto& commandOutcome = commandSetOutcome.GetCommand(task->Command());
                                commandOutcome.SetCommandLineValue(task->CommandLine());
                                commandOutcome.SetIsSelfOrcExecutable(task->IsSelfOrcExecutable());
                                commandOutcome.SetCreationTime(FromFileTime(task->CreationTime()));
                                commandOutcome.SetExitTime(FromFileTime(task->ExitTime()));

                                const auto& userTime = task->UserTime();
                                if (userTime)
                                {
                                    commandOutcome.SetUserTime(
                                        std::chrono::duration_cast<std::chrono::seconds>(*userTime));
                                }

                                const auto& kernelTime = task->KernelTime();
                                if (kernelTime)
                                {
                                    commandOutcome.SetKernelTime(
                                        std::chrono::duration_cast<std::chrono::seconds>(*kernelTime));
                                }

                                commandOutcome.SetExitCode(task->ExitCode());
                                commandOutcome.SetPid(task->Pid());
                                commandOutcome.SetKeyword(task->Command());

                                const auto& taskIoCounters = task->IoCounters();
                                if (taskIoCounters)
                                {
                                    commandOutcome.SetIOCounters(*taskIoCounters);
                                }
                            }
                        }

                        break;
                    }
                }
                NotifyTask(item);
            }
        });

    std::wstring strDbgHelpRef;

    bool bChildDebug = IsChildDebugActive(bGlobalChildDebug);

    if (bChildDebug)
    {
        WORD wArch;
        if (hr = FAILED(SystemDetails::GetArchitecture(wArch)))
            return hr;

        switch (wArch)
        {
            case PROCESSOR_ARCHITECTURE_AMD64:
                if (FAILED(hr = EmbeddedResource::ExtractValue(L"", L"WOLFLAUNCHER_DBGHELP64", strDbgHelpRef)))
                {
                    Log::Debug("WOLFLAUNCHER_DBGHELP64 is not set, won't use dbgelp [{}]", SystemError(hr));
                }
                break;
            case PROCESSOR_ARCHITECTURE_INTEL:
                if (FAILED(hr = EmbeddedResource::ExtractValue(L"", L"WOLFLAUNCHER_DBGHELP32", strDbgHelpRef)))
                {
                    Log::Debug("WOLFLAUNCHER_DBGHELP32 is not set, won't use dbgelp [{}]", SystemError(hr));
                }
                break;
        }
    }

    m_cmdAgent =
        std::make_unique<CommandAgent>(m_cmdAgentBuffer, m_ArchiveMessageBuffer, *m_cmdNotification, dwMaxTasks);

    hr = m_cmdAgent->Initialize(m_commandSet, bChildDebug, m_Temporary.Path, m_Restrictions, &m_cmdAgentBuffer);
    if (FAILED(hr))
    {
        Log::Error(
            L"WolfLauncher cmd agent failed during initialisation (output directory: '{}', [{}])",
            m_Temporary.Path,
            SystemError(hr));
        return hr;
    }

    if (!m_cmdAgent->start())
    {
        Log::Error("WolfLauncher cmd agent failed to start");
        return E_FAIL;
    }

    m_RefreshTimer = std::make_unique<Concurrency::timer<CommandMessage::Message>>(
        (unsigned int)(msRefresh == 0ms ? 1s : msRefresh).count(),
        CommandMessage::MakeRefreshRunningList(),
        &m_cmdAgentBuffer,
        true);
    m_RefreshTimer->start();

    if (m_ElapsedTime > 0ms)
    {
        m_KillerTimer = std::make_unique<Concurrency::timer<CommandMessage::Message>>(
            (unsigned int)m_ElapsedTime.count(), CommandMessage::MakeTerminateAllMessage(), &m_cmdAgentBuffer, false);
        m_KillerTimer->start();
    }
    return S_OK;
}

HRESULT WolfExecution::EnqueueCommands()
{
    HRESULT hr = E_FAIL;

    GetSystemTimeAsFileTime(&m_StartTime);

    for (const auto& command : m_Commands)
    {

        if (m_TasksByKeyword.find(command->Keyword()) != m_TasksByKeyword.end())
        {
            Log::Error(L"Task with keyword '{}' already exists, ignoring the later", command->Keyword());
        }
        else
        {
            m_TasksByKeyword[command->Keyword()] =
                std::make_shared<WolfTask>(GetKeyword(), command->Keyword(), m_journal);
        }
        if (!command->IsOptional())
        {
            {
                auto&& lock = m_outcome.Lock();
                auto& outcomeCommand = m_outcome.GetCommandSet(m_commandSet).GetCommand(command->Keyword());
                for (const auto& parameter : command->GetParameters())
                {
                    if (::HasFileOutput(parameter.Kind))
                    {
                        Outcome::Command::Output outputFile;
                        outputFile.SetName(parameter.Name);
                        outputFile.SetType(::ToOutputFileType(parameter.Kind));
                        outcomeCommand.GetOutput().emplace_back(std::move(outputFile));
                    }
                }
            }

            Concurrency::send(m_cmdAgentBuffer, command);
        }
    }

    return S_OK;
}

HRESULT WolfExecution::CompleteExecution()
{
    HRESULT hr = E_FAIL;

    auto last = CommandMessage::MakeDoneMessage();
    Concurrency::send(m_cmdAgentBuffer, last);

    try
    {
        if (m_cmdAgent != nullptr)
            Concurrency::agent::wait(m_cmdAgent.get(), (unsigned int)m_CmdTimeOut.count());
    }
    catch (Concurrency::operation_timed_out e)
    {
        Log::Error("Command agent completion timeout: {} msecs reached", m_CmdTimeOut.count());
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    if (m_pTermination)
    {
        Robustness::RemoveTerminationHandler(m_pTermination);
        m_pTermination.reset();
    }
    if (FAILED(hr = m_cmdAgent->UnInitialize()))
    {
        Log::Error("WolfLauncher cmd agent failed during unload [{}]", SystemError(hr));
        return hr;
    }
    return S_OK;
}

HRESULT WolfExecution::TerminateAllAndComplete()
{
    HRESULT hr = E_FAIL;

    auto terminatemessage = CommandMessage::MakeTerminateAllMessage();
    Concurrency::send(m_cmdAgentBuffer, terminatemessage);

    auto last = CommandMessage::MakeDoneMessage();
    Concurrency::send(m_cmdAgentBuffer, last);

    Log::Debug(
        L"Waiting {} secs for agent to complete closing job...",
        std::chrono::duration_cast<std::chrono::seconds>(m_CmdTimeOut).count());
    try
    {
        if (m_cmdAgent != nullptr)
            Concurrency::agent::wait(m_cmdAgent.get(), (unsigned int)m_CmdTimeOut.count());
    }
    catch (Concurrency::operation_timed_out e)
    {
        Log::Error(L"Command agent completion timeout: {} msecs reached", m_CmdTimeOut.count());
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }

    if (m_pTermination)
    {
        Robustness::RemoveTerminationHandler(m_pTermination);
        m_pTermination.reset();
    }

    return S_OK;
}

HRESULT WolfExecution::CompleteArchive(UploadMessage::ITarget* pUploadMessageQueue)
{
    HRESULT hr = E_FAIL;

    if (m_configStream != nullptr)
    {
        m_configStream->SetFilePointer(0LL, FILE_BEGIN, NULL);
        auto request = ArchiveMessage::MakeAddStreamRequest(L"Config.xml", m_configStream, false);
        Concurrency::send(m_ArchiveMessageBuffer, request);
    }

    if (m_localConfigStream != nullptr)
    {
        m_localConfigStream->SetFilePointer(0LL, FILE_BEGIN, NULL);
        auto request = ArchiveMessage::MakeAddStreamRequest(L"LocalConfig.xml", m_localConfigStream, false);
        Concurrency::send(m_ArchiveMessageBuffer, request);
    }

    auto request = ArchiveMessage::MakeCompleteRequest();
    Concurrency::send(m_ArchiveMessageBuffer, request);

    Log::Debug("WAITING FOR ARCHIVE to COMPLETE");

    try
    {
        if (m_archiveAgent != nullptr)
        {
            Concurrency::agent::wait(m_archiveAgent.get(), (unsigned int)m_ArchiveTimeOut.count());
        }
    }
    catch (Concurrency::operation_timed_out e)
    {
        Log::Error(
            "Command archive completion timeout: {} secs reached",
            std::chrono::duration_cast<std::chrono::seconds>(m_ArchiveTimeOut).count());
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }

    if (m_pTermination)
    {
        Robustness::RemoveTerminationHandler(m_pTermination);
        m_pTermination.reset();
    }

    auto archiveSize = [&]() {
        FileStream fs;

        if (FAILED(fs.ReadFrom(m_strOutputFullPath.c_str())))
            return 0LLU;

        return fs.GetSize();
    };

    GetSystemTimeAsFileTime(&m_ArchiveFinishTime);
    {
        auto start = Orc::ConvertTo(m_StartTime);
        auto end = Orc::ConvertTo(m_ArchiveFinishTime);
        auto duration = end - start;

        m_journal.Print(GetKeyword(), L"Archive", L"Ended (output: {} bytes, elapsed: {:%T})", archiveSize(), duration);

        Log::Info(
            L"{}: {} (took {} seconds, size {} bytes)",
            GetKeyword(),
            m_strArchiveFileName,
            duration.count() / 10000000,
            archiveSize());
    }

    if (pUploadMessageQueue && m_Output.UploadOutput)
    {
        if (m_Output.UploadOutput->IsFileUploaded(m_strOutputFileName))
        {
            switch (m_Output.UploadOutput->Operation)
            {
                case OutputSpec::UploadOperation::NoOp:
                    break;
                case OutputSpec::UploadOperation::Copy: {

                    auto request =
                        UploadMessage::MakeUploadFileRequest(m_strOutputFileName, m_strOutputFullPath, false);
                    Concurrency::send(pUploadMessageQueue, request);
                }
                break;
                case OutputSpec::UploadOperation::Move: {

                    auto request = UploadMessage::MakeUploadFileRequest(m_strOutputFileName, m_strOutputFullPath, true);
                    Concurrency::send(pUploadMessageQueue, request);
                }
                break;
            }
        }
    }

    return S_OK;
}
