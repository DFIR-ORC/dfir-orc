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

#include "LogFileWriter.h"

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

using namespace std;
using namespace std::chrono;

using namespace Concurrency;

using namespace Orc;
using namespace Orc::Command::Wolf;

HRESULT WolfExecution::SetRecipients(const std::vector<std::shared_ptr<WolfExecution::Recipient>> recipients)
{
    for (const auto& recipient : recipients)
    {
        for (const auto& strArchiveSpec : recipient->ArchiveSpec)
        {
            if (PathMatchSpec(m_strKeyword.c_str(), strArchiveSpec.c_str()))
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
        log::Error(_L_, E_FAIL, L"No valid archive file name specified, archive agent is not created\r\n");
        return E_FAIL;
    }
    else
    {
        log::Verbose(_L_, L"INFO: Archive file name specified is \"%s\"\r\n", m_strArchiveName.c_str());
    }

    if (IsPathComplete(m_strArchiveName.c_str()) == S_FALSE)
    {
        // A simple file name is used

        if (FAILED(hr = CommandAgent::ApplyPattern(m_strArchiveName, L"", L"", m_strArchiveFileName)))
            return hr;

        if (m_Output.Type != OutputSpec::Kind::None && m_RepeatBehavior == CreateNew)
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
            log::Error(_L_, E_INVALIDARG, L"no valid output directory specified\r\n");
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
            log::Error(_L_, hr, L"Unable to extract archive file name\r\n");
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

HRESULT WolfExecution::CreateArchiveAgent()
{
    HRESULT hr = E_FAIL;

    m_archiveNotification = std::make_unique<
        call<ArchiveNotification::Notification>>([this](const ArchiveNotification::Notification& item) {
        if (SUCCEEDED(item->GetHResult()))
        {
            switch (item->GetType())
            {
                case ArchiveNotification::ArchiveStarted:
                    log::Info(_L_, L"%*s: %s started\r\n", m_dwLongerTaskKeyword + 20, L"ARC", item->Keyword().c_str());
                    break;
                case ArchiveNotification::FileAddition:
                    log::Info(
                        _L_, L"%*s: File %s added\r\n", m_dwLongerTaskKeyword + 20, L"ARC", item->Keyword().c_str());
                    break;
                case ArchiveNotification::DirectoryAddition:
                    log::Info(
                        _L_,
                        L"%*s: Directory %s added\r\n",
                        m_dwLongerTaskKeyword + 20,
                        L"ARC",
                        item->Keyword().c_str());
                    break;
                case ArchiveNotification::StreamAddition:
                    log::Info(
                        _L_, L"%*s: Output %s added\r\n", m_dwLongerTaskKeyword + 20, L"ARC", item->Keyword().c_str());
                    break;
                case ArchiveNotification::ArchiveComplete:
                    log::Info(
                        _L_, L"%*s: %s is complete\r\n", m_dwLongerTaskKeyword + 20, L"ARC", item->Keyword().c_str());
                    break;
            }
        }
        else
        {
            log::Error(
                _L_,
                item->GetHResult(),
                L"ArchiveOperation: Operation for %s failed \"%s\"\r\n",
                item->Keyword().c_str(),
                item->Description().c_str());
        }
        return;
    });

    m_archiveAgent =
        std::make_unique<ArchiveAgent>(_L_, m_ArchiveMessageBuffer, m_ArchiveMessageBuffer, *m_archiveNotification);
    if (!m_archiveAgent->start())
    {
        log::Error(_L_, E_FAIL, L"Start for archive Agent failed\r\n");
        return E_FAIL;
    }

    if (!m_Recipients.empty())
    {
        if (m_strOutputFullPath.empty())
        {
            log::Error(_L_, E_FAIL, L"Invalid empty output file name\r\n");
            return E_FAIL;
        }

        auto pOutputStream = std::make_shared<FileStream>(_L_);

        if (FAILED(hr = pOutputStream->WriteTo(m_strOutputFullPath.c_str())))
        {
            log::Error(_L_, hr, L"Failed open file %s to write\r\n", m_strOutputFullPath.c_str());
            return hr;
        }

        auto pEncodingStream = std::make_shared<EncodeMessageStream>(_L_);

        for (auto& recipient : m_Recipients)
        {
            if (FAILED(hr = pEncodingStream->AddRecipient(recipient->Certificate)))
            {
                log::Error(_L_, hr, L"Failed to add certificate for recipient %s\r\n", recipient->Name.c_str());
                return hr;
            }
        }
        if (FAILED(hr = pEncodingStream->Initialize(pOutputStream)))
        {
            log::Error(_L_, hr, L"Failed initialize encoding stream for %s\r\n", m_strOutputFullPath.c_str());
            return hr;
        }

        std::shared_ptr<ByteStream> pFinalStream;

        if (UseJournalWhenEncrypting())
        {
            auto pJournalingStream = std::make_shared<JournalingStream>(_L_);

            if (FAILED(hr = pJournalingStream->Open(pEncodingStream)))
            {
                log::Error(_L_, hr, L"Failed open journaling stream to write\r\n");
                return hr;
            }
            pFinalStream = pJournalingStream;
        }
        else
        {
            auto pAccumulatingStream = std::make_shared<AccumulatingStream>(_L_);

            if (FAILED(hr = pAccumulatingStream->Open(pEncodingStream, m_Temporary.Path, 100 * 1024 * 1024)))
            {
                log::Error(_L_, hr, L"Failed open accumulating stream to write\r\n");
                return hr;
            }
            pFinalStream = pAccumulatingStream;
        }

        if (TeeClearTextOutput())
        {
            auto pClearStream = std::make_shared<FileStream>(_L_);

            if (FAILED(hr = pClearStream->WriteTo(m_strArchiveFullPath.c_str())))
            {
                log::Error(_L_, hr, L"Failed initialize file stream for %s\r\n", m_strArchiveFullPath.c_str());
                return hr;
            }

            auto pTeeTream = std::make_shared<TeeStream>(_L_);

            if (FAILED(hr = pTeeTream->Open({pClearStream, pFinalStream})))
            {
                log::Error(
                    _L_,
                    hr,
                    L"Failed initialize tee stream for %s & %s\r\n",
                    m_strOutputFileName.c_str(),
                    m_strArchiveFullPath.c_str());
                return hr;
            }

            pFinalStream = pTeeTream;
        }

        ArchiveFormat fmt = Archive::GetArchiveFormat(m_strArchiveFileName);

        Concurrency::send(
            m_ArchiveMessageBuffer,
            ArchiveMessage::MakeOpenRequest(m_strArchiveFileName, fmt, pFinalStream, m_strCompressionLevel));
    }
    else
    {
        auto pOutputStream = std::make_shared<FileStream>(_L_);

        if (FAILED(hr = pOutputStream->WriteTo(m_strOutputFullPath.c_str())))
        {
            log::Error(_L_, hr, L"Failed open file %s to write\r\n", m_strOutputFullPath.c_str());
            return hr;
        }

        ArchiveFormat fmt = Archive::GetArchiveFormat(m_strArchiveFileName);

        Concurrency::send(
            m_ArchiveMessageBuffer,
            ArchiveMessage::MakeOpenRequest(m_strArchiveFileName, fmt, pOutputStream, m_strCompressionLevel));
    }
    return S_OK;
}

HRESULT WolfExecution::AddProcessStatistics(ITableOutput& output, const CommandNotification::Notification& notification)
{
    SystemDetails::WriteComputerName(output);

    output.WriteString(notification->GetKeyword().c_str());
    output.WriteInteger(notification->GetProcessID());
    output.WriteInteger(notification->GetExitCode());

    PPROCESS_TIMES pTimes = notification->GetProcessTimes();

    if (pTimes != nullptr)
    {
        output.WriteFileTime(pTimes->CreationTime);
        output.WriteFileTime(pTimes->ExitTime);

        LARGE_INTEGER UserTime;
        UserTime.HighPart = pTimes->UserTime.dwHighDateTime;
        UserTime.LowPart = pTimes->UserTime.dwLowDateTime;

        output.WriteInteger(UserTime.QuadPart);

        LARGE_INTEGER KernelTime;
        KernelTime.HighPart = pTimes->KernelTime.dwHighDateTime;
        KernelTime.LowPart = pTimes->KernelTime.dwLowDateTime;

        output.WriteInteger(KernelTime.QuadPart);
    }
    else
    {
        output.WriteNothing();
        output.WriteNothing();
        output.WriteNothing();
        output.WriteNothing();
    }

    PIO_COUNTERS pIOCounters = notification->GetProcessIoCounters();

    if (pIOCounters != nullptr)
    {
        output.WriteInteger(pIOCounters->ReadOperationCount);
        output.WriteInteger(pIOCounters->ReadTransferCount);
        output.WriteInteger(pIOCounters->WriteOperationCount);
        output.WriteInteger(pIOCounters->WriteTransferCount);
        output.WriteInteger(pIOCounters->OtherOperationCount);
        output.WriteInteger(pIOCounters->OtherTransferCount);
    }
    else
    {
        output.WriteNothing();
        output.WriteNothing();
        output.WriteNothing();
        output.WriteNothing();
        output.WriteNothing();
        output.WriteNothing();
    }
    output.WriteEndOfLine();

    return S_OK;
}

HRESULT WolfExecution::AddJobStatistics(ITableOutput& output, const CommandNotification::Notification& notification)
{
    HRESULT hr = E_FAIL;

    SystemDetails::WriteComputerName(output);

    output.WriteString(notification->GetKeyword().c_str());
    output.WriteFileTime(m_StartTime);
    output.WriteFileTime(m_FinishTime);

    LARGE_INTEGER start, end;
    start.HighPart = m_StartTime.dwHighDateTime;
    start.LowPart = m_StartTime.dwLowDateTime;

    end.HighPart = m_FinishTime.dwHighDateTime;
    end.LowPart = m_FinishTime.dwLowDateTime;

    LARGE_INTEGER elapsed;

    elapsed.QuadPart = (end.QuadPart - start.QuadPart) / (10000 * 1000);

    output.WriteInteger(elapsed.QuadPart);

    PJOB_STATISTICS pJobStats = notification->GetJobStatictics();

    if (pJobStats != nullptr)
    {
        output.WriteInteger(pJobStats->TotalUserTime.QuadPart);
        output.WriteInteger(pJobStats->TotalKernelTime.QuadPart);
        output.WriteInteger(pJobStats->TotalPageFaultCount);
        output.WriteInteger(pJobStats->TotalProcesses);
        output.WriteInteger(pJobStats->ActiveProcesses);
        output.WriteInteger(pJobStats->TotalTerminatedProcesses);
        output.WriteInteger(pJobStats->PeakProcessMemoryUsed);
        output.WriteInteger(pJobStats->PeakJobMemoryUsed);

        output.WriteInteger(pJobStats->IoInfo.ReadOperationCount);
        output.WriteInteger(pJobStats->IoInfo.ReadTransferCount);
        output.WriteInteger(pJobStats->IoInfo.WriteOperationCount);
        output.WriteInteger(pJobStats->IoInfo.WriteTransferCount);
        output.WriteInteger(pJobStats->IoInfo.OtherOperationCount);
        output.WriteInteger(pJobStats->IoInfo.OtherTransferCount);
    }
    else
    {
        output.WriteNothing();
        output.WriteNothing();
        output.WriteNothing();
        output.WriteNothing();
        output.WriteNothing();
        output.WriteNothing();
        output.WriteNothing();
        output.WriteNothing();

        output.WriteNothing();
        output.WriteNothing();
        output.WriteNothing();
        output.WriteNothing();
        output.WriteNothing();
        output.WriteNothing();
    }

    SystemDetails::WriteDescriptionString(output);
    SystemDetails::WriteProductype(output);

    output.WriteEndOfLine();
    return S_OK;
}

HRESULT WolfExecution::NotifyTask(const CommandNotification::Notification& item)
{
    HRESULT hr = E_FAIL;

    shared_ptr<WolfTask> task;

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
        vector<CommandMessage::Message> actions;
        task->ApplyNotification(m_dwLongerTaskKeyword, item, actions);

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
    HRESULT hr = E_FAIL;

    m_pTermination = std::make_shared<WOLFExecutionTerminate>(m_strKeyword, this);
    Robustness::AddTerminationHandler(m_pTermination);

    if (m_ProcessStatisticsOutput.Type & OutputSpec::Kind::TableFile)
    {
        m_ProcessStatisticsWriter = TableOutput::GetWriter(_L_, m_ProcessStatisticsOutput);

        if (m_ProcessStatisticsWriter == nullptr)
        {
            log::Error(_L_, E_FAIL, L"Failed to initialize ProcessStatistics writer\r\n");
        }
    }

    if (m_JobStatisticsOutput.Type & OutputSpec::Kind::TableFile)
    {
        m_JobStatisticsWriter = TableOutput::GetWriter(_L_, m_JobStatisticsOutput);
        if (m_JobStatisticsWriter == nullptr)
        {
            log::Error(_L_, E_FAIL, L"Failed to initialize JobStatistics writer\r\n");
        }
    }

    m_cmdNotification = std::make_unique<call<CommandNotification::Notification>>(
        [this](const CommandNotification::Notification& item) {
            HRESULT hr = E_FAIL;

            if (item)
            {
                switch (item->GetEvent())
                {
                    case CommandNotification::Started:
                    {
                        auto taskiter = m_TasksByKeyword.find(item->GetKeyword());
                        if (taskiter != m_TasksByKeyword.end())
                            m_TasksByPID[static_cast<DWORD>(item->GetProcessID())] = taskiter->second;
                        else
                        {
                            log::Error(_L_, E_FAIL, L"New task %s could not be found\r\n", item->GetKeyword().c_str());
                        }
                    }
                    break;
                    case CommandNotification::Terminated:
                        AddProcessStatistics(*m_ProcessStatisticsWriter, item);
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
                        log::Verbose(_L_, L"No tasks are currently running\r\n");
                        break;
                    case CommandNotification::JobProcessLimit:
                        log::Info(_L_, L"%*s: Process number limit!\r\n", m_dwLongerTaskKeyword + 20, L"JOB");
                        break;
                    case CommandNotification::JobMemoryLimit:
                        log::Info(_L_, L"%*s: Memory limit!\r\n", m_dwLongerTaskKeyword + 20, L"JOB");
                        break;
                    case CommandNotification::JobTimeLimit:
                        log::Info(_L_, L"%*s: CPU Time limit!\r\n", m_dwLongerTaskKeyword + 20, L"JOB");
                        break;
                    case CommandNotification::AllTerminated:
                        log::Info(
                            _L_, L"%*s: Job was autoritatively terminated!\r\n", m_dwLongerTaskKeyword + 20, L"JOB");
                        break;
                    case CommandNotification::Done:
                        GetSystemTimeAsFileTime(&m_FinishTime);
                        {
                            auto start = Orc::ConvertTo(m_StartTime);
                            auto end = Orc::ConvertTo(m_FinishTime);
                            auto duration = end - start;

                            log::Info(
                                _L_,
                                L"%*s: Complete! (commands took %I64d seconds)\r\n",
                                m_dwLongerTaskKeyword + 20,
                                m_strKeyword.c_str(),
                                duration.count() / 10000000);
                        }

                        AddJobStatistics(*m_JobStatisticsWriter, item);
                        break;
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
                if (FAILED(hr = EmbeddedResource::ExtractValue(_L_, L"", L"WOLFLAUNCHER_DBGHELP64", strDbgHelpRef)))
                {
                    log::Verbose(_L_, L"WOLFLAUNCHER_DBGHELP64 is not set, won't use dbgelp\r\n");
                }
                break;
            case PROCESSOR_ARCHITECTURE_INTEL:
                if (FAILED(hr = EmbeddedResource::ExtractValue(_L_, L"", L"WOLFLAUNCHER_DBGHELP32", strDbgHelpRef)))
                {
                    log::Verbose(_L_, L"WOLFLAUNCHER_DBGHELP32 is not set, won't use dbgelp\r\n");
                }
                break;
        }
    }

    m_cmdAgent =
        std::make_unique<CommandAgent>(_L_, m_cmdAgentBuffer, m_ArchiveMessageBuffer, *m_cmdNotification, dwMaxTasks);

    if (FAILED(
            hr =
                m_cmdAgent->Initialize(m_strKeyword, bChildDebug, m_Temporary.Path, m_Restrictions, &m_cmdAgentBuffer)))
    {
        log::Error(
            _L_,
            hr,
            L"WolfLauncher cmd agent failed during initialisation (OutputDir is %s)\r\n",
            m_Temporary.Path.c_str());
        return hr;
    }
    if (!m_cmdAgent->start())
    {
        log::Error(_L_, E_FAIL, L"WolfLauncher cmd agent failed to start\r\n");
        return E_FAIL;
    }

    m_RefreshTimer = std::make_unique<timer<CommandMessage::Message>>(
        (unsigned int)(msRefresh == 0ms ? 1s : msRefresh).count(),
        CommandMessage::MakeRefreshRunningList(),
        &m_cmdAgentBuffer,
        true);
    m_RefreshTimer->start();

    if (m_ElapsedTime > 0ms)
    {
        m_KillerTimer = std::make_unique<timer<CommandMessage::Message>>(
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
            log::Error(
                _L_,
                E_INVALIDARG,
                L"Task with keyword %s already exists, ignoring the later\r\n",
                command->Keyword().c_str());
        }
        else
        {
            m_TasksByKeyword[command->Keyword()] = make_shared<WolfTask>(_L_, command->Keyword());
        }
        if (!command->IsOptional())
            Concurrency::send(m_cmdAgentBuffer, command);
    }

    return S_OK;
}

HRESULT WolfExecution::CompleteExecution()
{
    HRESULT hr = E_FAIL;

    auto last = CommandMessage::MakeDoneMessage();
    send(m_cmdAgentBuffer, last);

    try
    {
        if (m_cmdAgent != nullptr)
            agent::wait(m_cmdAgent.get(), (unsigned int)m_CmdTimeOut.count());
    }
    catch (operation_timed_out e)
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT),
            L"Command agent completion timeout (%I64d msecs) reached\r\n",
            m_CmdTimeOut.count());
        return hr;
    }
    if (m_pTermination)
    {
        Robustness::RemoveTerminationHandler(m_pTermination);
        m_pTermination.reset();
    }
    if (FAILED(hr = m_cmdAgent->UnInitialize()))
    {
        log::Error(_L_, hr, L"WolfLauncher cmd agent failed during unload\r\n");
        return hr;
    }
    return S_OK;
}

HRESULT WolfExecution::TerminateAllAndComplete()
{
    HRESULT hr = E_FAIL;

    auto terminatemessage = CommandMessage::MakeTerminateAllMessage();
    send(m_cmdAgentBuffer, terminatemessage);

    auto last = CommandMessage::MakeDoneMessage();
    send(m_cmdAgentBuffer, last);

    log::Verbose(
        _L_, L"Waiting %d secs for agent to complete closing job...\r\n", duration_cast<seconds>(m_CmdTimeOut).count());
    try
    {
        if (m_cmdAgent != nullptr)
            agent::wait(m_cmdAgent.get(), (unsigned int)m_CmdTimeOut.count());
    }
    catch (operation_timed_out e)
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT),
            L"Command agent completion timeout (%d msecs) reached\r\n",
            m_CmdTimeOut.count());
        return hr;
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
    m_ProcessStatisticsWriter->Close();
    m_JobStatisticsWriter->Close();

    if (VerifyFileExists(m_ProcessStatisticsOutput.Path.c_str()) == S_OK)
    {
        send(
            m_ArchiveMessageBuffer,
            ArchiveMessage::MakeAddFileRequest(
                L"ProcessStatistics.csv", m_ProcessStatisticsOutput.Path, false, 0L, true));
    }

    if (VerifyFileExists(m_JobStatisticsOutput.Path.c_str()) == S_OK)
    {
        send(
            m_ArchiveMessageBuffer,
            ArchiveMessage::MakeAddFileRequest(L"JobStatistics.csv", m_JobStatisticsOutput.Path, false, 0L, true));
    }

    if (m_configStream != nullptr)
    {
        m_configStream->SetFilePointer(0LL, FILE_BEGIN, NULL);
        send(m_ArchiveMessageBuffer, ArchiveMessage::MakeAddStreamRequest(L"Config.xml", m_configStream, false, 0L));
    }
    if (m_localConfigStream != nullptr)
    {
        m_localConfigStream->SetFilePointer(0LL, FILE_BEGIN, NULL);
        send(
            m_ArchiveMessageBuffer,
            ArchiveMessage::MakeAddStreamRequest(L"LocalConfig.xml", m_localConfigStream, false, 0L));
    }

    send(m_ArchiveMessageBuffer, ArchiveMessage::MakeCompleteRequest());

    log::Verbose(_L_, L"WAITING FOR ARCHIVE to COMPLETE\r\n");

    try
    {
        if (m_archiveAgent != nullptr)
            agent::wait(m_archiveAgent.get(), (unsigned int)m_ArchiveTimeOut.count());
    }
    catch (operation_timed_out e)
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT),
            L"Command archive completion timeout (%d secs) reached\r\n",
            duration_cast<seconds>(m_ArchiveTimeOut).count());
        return hr;
    }
    if (m_pTermination)
    {
        Robustness::RemoveTerminationHandler(m_pTermination);
        m_pTermination.reset();
    }

    auto archiveSize = [&]() {
        FileStream fs (_L_);

        if (FAILED(fs.ReadFrom(m_strOutputFullPath.c_str())))
            return 0LLU;

        return fs.GetSize();
    };
    
    GetSystemTimeAsFileTime(&m_ArchiveFinishTime);
    {
        auto start = Orc::ConvertTo(m_StartTime);
        auto end = Orc::ConvertTo(m_ArchiveFinishTime);
        auto duration = end - start;

        log::Info(
            _L_,
            L"%*s: %s (took %I64d seconds, size %I64d bytes)\r\n",
            m_dwLongerTaskKeyword + 20,
            m_strKeyword.c_str(),
            m_strArchiveFileName.c_str(),
            duration.count() / 10000000,
            archiveSize());
    }

    if (pUploadMessageQueue && m_Output.UploadOutput)
    {
        if (m_Output.UploadOutput->IsFileUploaded(_L_, m_strOutputFileName))
        {
            switch (m_Output.UploadOutput->Operation)
            {
                case OutputSpec::UploadOperation::NoOp:
                    break;
                case OutputSpec::UploadOperation::Copy:
                    Concurrency::send(
                        pUploadMessageQueue,
                        UploadMessage::MakeUploadFileRequest(m_strOutputFileName, m_strOutputFullPath, false));
                    break;
                case OutputSpec::UploadOperation::Move:
                    Concurrency::send(
                        pUploadMessageQueue,
                        UploadMessage::MakeUploadFileRequest(m_strOutputFileName, m_strOutputFullPath, true));
                    break;
            }
        }
    }

    return S_OK;
}
