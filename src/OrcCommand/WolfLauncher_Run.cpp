//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "WolfLauncher.h"

#include "Robustness.h"

#include "EmbeddedResource.h"
#include "SystemDetails.h"
#include "SystemIdentity.h"
#include "LogFileWriter.h"
#include "JobObject.h"
#include "StructuredOutputWriter.h"
#include "Convert.h"
#include "FileStream.h"

#include <boost/logic/tribool.hpp>
#include <boost/scope_exit.hpp>

using namespace std;
using namespace Concurrency;

using namespace Orc;
using namespace Orc::Command::Wolf;

HRESULT Main::InitializeUpload(const OutputSpec::Upload& uploadspec)
{
    HRESULT hr = E_FAIL;
    switch (uploadspec.Method)
    {
        case OutputSpec::None:
            break;
        default: {
            m_pUploadMessageQueue = std::make_unique<UploadMessage::UnboundedMessageBuffer>();
            m_pUploadNotification = std::make_unique<call<UploadNotification::Notification>>(
                [this](const UploadNotification::Notification& item) {
                    if (SUCCEEDED(item->GetHResult()))
                    {
                        switch (item->GetType())
                        {
                            case UploadNotification::Started:
                                log::Info(_L_, L"UPLOAD: %s started\r\n", item->Keyword().c_str());
                                break;
                            case UploadNotification::FileAddition:
                                log::Info(_L_, L"UPLOAD: File %s added to upload queue\r\n", item->Keyword().c_str());
                                break;
                            case UploadNotification::DirectoryAddition:
                                log::Info(
                                    _L_, L"UPLOAD: Directory %s added to upload queue\r\n", item->Keyword().c_str());
                                break;
                            case UploadNotification::StreamAddition:
                                log::Info(_L_, L"UPLOAD: Output %s added to upload queue\r\n", item->Keyword().c_str());
                                break;
                            case UploadNotification::UploadComplete:
                                log::Info(_L_, L"UPLOAD: File %s is uploaded\r\n", item->Keyword().c_str());
                                break;
                            case UploadNotification::JobComplete:
                                log::Info(_L_, L"UPLOAD: %s is complete\r\n", item->Keyword().c_str());
                                break;
                        }
                    }
                    else
                    {
                        log::Error(
                            _L_,
                            item->GetHResult(),
                            L"UPLOAD: Operation for %s failed \"%s\"\r\n",
                            item->Keyword().c_str(),
                            item->Description().c_str());
                    }
                    return;
                });

            m_pUploadAgent = UploadAgent::CreateUploadAgent(
                _L_, uploadspec, *m_pUploadMessageQueue, *m_pUploadMessageQueue, *m_pUploadNotification);

            if (m_pUploadAgent == nullptr)
            {
                // Agent creation failed, reset buffers too
                m_pUploadMessageQueue = nullptr;
                m_pUploadNotification = nullptr;
            }
            else
            {
                // Agent created, run it!
                if (!m_pUploadAgent->start())
                {
                    log::Error(_L_, E_FAIL, L"Failed to start upload agent\r\n");
                    return E_FAIL;
                }
            }
        }
        break;
    }
    return S_OK;
}

HRESULT Orc::Command::Wolf::Main::UploadSingleFile(const std::wstring& fileName, const std::wstring& filePath)
{
    if (VerifyFileExists(filePath.c_str()) == S_OK)
    {
        if (m_pUploadMessageQueue && config.Output.UploadOutput)
        {
            switch (config.Output.UploadOutput->Operation)
            {
                case OutputSpec::UploadOperation::NoOp:
                    break;
                case OutputSpec::UploadOperation::Copy:
                    Concurrency::send(
                        m_pUploadMessageQueue.get(), UploadMessage::MakeUploadFileRequest(fileName, filePath, false));
                    break;
                case OutputSpec::UploadOperation::Move:
                    Concurrency::send(
                        m_pUploadMessageQueue.get(), UploadMessage::MakeUploadFileRequest(fileName, filePath, true));
                    break;
            }
        }
    }
    return S_OK;
}

HRESULT Main::CompleteUpload()
{
    HRESULT hr = E_FAIL;

    if (m_pUploadAgent)
    {
        Concurrency::send(m_pUploadMessageQueue.get(), UploadMessage::MakeCompleteRequest());

        agent::wait(m_pUploadAgent.get());
    }

    return S_OK;
}

boost::logic::tribool Main::SetWERDontShowUI(DWORD dwNewValue)
{
    boost::logic::tribool retval = boost::indeterminate;

    HKEY hWERKey = nullptr;
    DWORD lasterror = RegOpenKey(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\Windows Error Reporting", &hWERKey);
    if (lasterror != ERROR_SUCCESS)
    {
        log::Error(_L_, HRESULT_FROM_WIN32(lasterror), L"Failed to open WER registry key\r\n");
        return retval;
    }

    DWORD dwValue = 0L;
    DWORD dwValueSize = sizeof(dwValue);
    lasterror = RegQueryValueEx(hWERKey, L"DontShowUI", NULL, NULL, (LPBYTE)&dwValue, &dwValueSize);
    if (lasterror != ERROR_SUCCESS)
    {
        log::Error(_L_, HRESULT_FROM_WIN32(lasterror), L"Failed to open WER registry DontShowUI value\r\n");
        return retval;
    }
    if (dwValue != dwNewValue)
    {

        lasterror = RegSetValueEx(hWERKey, L"DontShowUI", NULL, REG_DWORD, (LPBYTE)&dwNewValue, sizeof(dwNewValue));
        if (lasterror != ERROR_SUCCESS)
        {
            log::Error(_L_, HRESULT_FROM_WIN32(lasterror), L"Failed to set WER registry DontShowUI value\r\n");
            return retval;
        }

        if (dwNewValue)
        {
            log::Info(_L_, L"\r\nWER user interface is now disabled\r\n");
        }
        else
        {
            log::Info(_L_, L"\r\nWER user interface is now enabled\r\n");
        }
        if (dwValue)
            retval = true;
        else
            retval = false;
    }
    return retval;
}

HRESULT Orc::Command::Wolf::Main::CreateAndUploadOutline()
{
    FILETIME StartTime;
    GetSystemTimeAsFileTime(&StartTime);

    try
    {
        auto options = std::make_unique<StructuredOutput::JSON::Options>();
        options->Encoding = OutputSpec::Encoding::UTF8;
        options->bPrettyPrint = true;

        auto writer = StructuredOutputWriter::GetWriter(_L_, config.Outline, std::move(options));
        if (writer == nullptr)
        {
            log::Error(
                _L_, E_INVALIDARG, L"Failed to create writer for outline file %s\r\n", config.Outline.Path.c_str());
            return E_INVALIDARG;
        }

        writer->BeginElement(L"dfir-orc");
        {
            writer->WriteNamed(L"version", L"1.0");
            writer->WriteNamed(L"dfir_orc_id", kOrcFileVerStringW);

            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            writer->WriteNamed(L"time", ft);

            auto mothership_id = SystemDetails::GetParentProcessId(_L_);
            if (mothership_id.is_ok())
            {
                auto mothership_cmdline = SystemDetails::GetCmdLine(_L_, mothership_id.unwrap());
                if (mothership_cmdline.is_ok())
                    writer->WriteNamed(L"command", mothership_cmdline.unwrap().c_str());
            }
            
            writer->BeginCollection(L"archives");
            for (const auto& exec : m_wolfexecs)
            {
                if (!exec->IsOptional())
                {
                    writer->BeginElement(nullptr);
                    {
                        writer->WriteNamed(L"keyword", exec->GetKeyword().c_str());
                        writer->WriteNamed(L"file", exec->GetArchiveFileName().c_str());
                        writer->BeginCollection(L"commands");
                        for (const auto& command : exec->GetCommands())
                        {
                            if (!command->IsOptional())
                            {
                                writer->Write(command->Keyword().c_str());
                            }
                        }
                        writer->EndCollection(L"commands");
                    }
                    writer->EndElement(nullptr);
                }
            }
            writer->EndCollection(L"archives");

            writer->WriteNamed(L"output", config.Output.Path.c_str());
            writer->WriteNamed(L"temp", config.TempWorkingDir.Path.c_str());

            SystemIdentity::Write(writer);
        }
        writer->EndElement(L"dfir-orc");

        writer->Close();
    }
    catch (std::exception& e)
    {
        std::cerr << "std::exception during outline creation" << std::endl;
        std::cerr << "Caught " << e.what() << std::endl;
        std::cerr << "Type " << typeid(e).name() << std::endl;
        return E_ABORT;
    }
    catch (...)
    {
        std::cerr << "Exception during outline creation" << std::endl;
        return E_ABORT;
    }

    auto outlineSize = [&]() {
        FileStream fs(_L_);

        if (FAILED(fs.ReadFrom(config.Outline.Path.c_str())))
            return 0LLU;

        return fs.GetSize();
    };

    FILETIME FinishTime;
    GetSystemTimeAsFileTime(&FinishTime);
    {
        auto start = Orc::ConvertTo(StartTime);
        auto end = Orc::ConvertTo(FinishTime);
        auto duration = end - start;

        log::Info(
            _L_,
            L"Outline               : %s (took %I64d seconds, size %I64d bytes)\r\n",
            config.Outline.FileName.c_str(),
            duration.count() / 10000000,
            outlineSize());
    }

    if (std::filesystem::exists(config.Outline.Path))
    {
        if (auto hr = UploadSingleFile(config.Outline.FileName, config.Outline.Path); FAILED(hr))
        {
            log::Error(_L_, hr, L"Failed to upload outline file\r\n");
        }
    }
    return S_OK;
}

HRESULT Main::SetLauncherPriority(WolfPriority priority)
{
    switch (priority)
    {
        case Low:
            SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            break;
        case Normal:
            SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
            break;
        case High:
            SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
            break;
    }

    return S_OK;
}

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    switch (config.SelectedAction)
    {
        case WolfLauncherAction::Execute:
            return Run_Execute();
        case WolfLauncherAction::Keywords:
            return Run_Keywords();
        default:
            return E_NOTIMPL;
    }
}

HRESULT Main::Run_Execute()
{
    HRESULT hr = E_FAIL;
    bool bJobWasModified = false;

    if (config.Log.Type != OutputSpec::Kind::None)
    {
        auto stream_log = std::make_shared<LogFileWriter>(0x1000);
        stream_log->SetConsoleLog(_L_->ConsoleLog());
        stream_log->SetDebugLog(_L_->DebugLog());
        stream_log->SetVerboseLog(_L_->VerboseLog());

        auto byteStream = ByteStream::GetStream(stream_log, config.Log);

        if (byteStream->GetSize() == 0LLU)
        {
            // Writing BOM
            ULONGLONG bytesWritten = 0LLU;
            BYTE bom[2] = {0xFF, 0xFE};
            if (FAILED(hr = byteStream->Write(bom, 2 * sizeof(BYTE), &bytesWritten)))
            {
                log::Warning(_L_, hr, L"Failed to write BOM to log stream\r\n");
                byteStream = nullptr;
            }
        }

        if (byteStream)
        {
            if (FAILED(hr = _L_->LogToStream(byteStream)))
            {
                log::Warning(_L_, hr, L"Failed to associate log stream with logger\r\n");
            }
        }
    }

    if (config.Output.UploadOutput)
    {
        if (FAILED(hr = InitializeUpload(*config.Output.UploadOutput)))
        {
            log::Error(_L_, hr, L"Failed to initalise upload as requested, no upload will be performed\r\n");
        }
    }

    if (FAILED(hr = SetDefaultAltitude()))
    {
        log::Warning(_L_, hr, L"Failed to configure default altitude\r\n");
    }

    if (FAILED(hr = SetLauncherPriority(config.Priority)))
    {
        log::Warning(_L_, hr, L"Failed to configure launcher priority\r\n");
    }

    if (config.PowerState != WolfPowerState::Unmodified)
    {
        EXECUTION_STATE previousState =
            SetThreadExecutionState(static_cast<EXECUTION_STATE>(config.PowerState | ES_CONTINUOUS));
    }

    BOOST_SCOPE_EXIT(hr) { SetThreadExecutionState(ES_CONTINUOUS); }
    BOOST_SCOPE_EXIT_END;

    if (config.Outline.IsStructuredFile())
    {
        if (auto hr = CreateAndUploadOutline(); FAILED(hr))
        {
            log::Warning(_L_, hr, L"Failed to create outline file %s\r\n", config.Outline.Path.c_str());
        }
    }

    auto job = JobObject::GetJobObject(_L_, GetCurrentProcess());

    if (job.IsValid())
    {
        // Checking in WOLFLauncher is running within a Windows Job object
        if (!job.IsBreakAwayAllowed())
        {
            DWORD dwMajor = 0L, dwMinor = 0L;
            SystemDetails::GetOSVersion(dwMajor, dwMinor);
            if (dwMajor < 6 || (dwMajor == 6 && dwMinor < 2))
            {
                // Job object does not allow breakaway.
                if (FAILED(hr = job.AllowBreakAway()))
                {
                    log::Error(_L_, E_INVALIDARG, L"Running within a job that won't allow breakaway, exiting\r\n");
                    return E_FAIL;
                }
                else
                {
                    bJobWasModified = true;
                    log::Info(_L_, L"INFO: Running within a job, it has been configured to allow breakaway\r\n");
                }
            }
            else
            {
                log::Verbose(
                    _L_,
                    L"WolfLauncher is within a job that won't allow breakaway. Windows 8.x allows nested job. "
                    L"Giving it a try!!\r\n");
            }
        }
    }

    boost::logic::tribool WERDontShowUIStatus = boost::logic::indeterminate;

    if (config.bWERDontShowUI)
    {
        WERDontShowUIStatus = SetWERDontShowUI(1L);
    }
    BOOST_SCOPE_EXIT(&WERDontShowUIStatus, this_)
    {
        if (!WERDontShowUIStatus)
        {
            // Value mas modified from false to true
            this_->SetWERDontShowUI(0L);
        }
        else if (WERDontShowUIStatus)
        {
            // Value was modified from true to false
            this_->SetWERDontShowUI(1L);
        }
        else
        {
            // Value was not modified, do nothing
        }
    }
    BOOST_SCOPE_EXIT_END;

    for (const auto& exec : m_wolfexecs)
    {

        if (exec->IsOptional())
            continue;

        if (m_pConfigStream != nullptr)
        {
            // Rewind the config file stream to enable the config to be "not empty" :-)
            m_pConfigStream->SetFilePointer(0LL, FILE_BEGIN, NULL);
        }

        exec->SetUseJournalWhenEncrypting(config.bUseJournalWhenEncrypting);

        bool bDebug = exec->IsChildDebugActive(config.bChildDebug);

        wstring strRecipients;
        bool bFirst = true;
        for (const auto& rec : exec->Recipients())
        {
            if (!bFirst)
            {
                strRecipients += L", ";
                strRecipients += rec->Name;
            }
            else
            {
                strRecipients = L" (recipients ";
                strRecipients += rec->Name;
                bFirst = false;
            }
        }
        if (!strRecipients.empty())
        {
            strRecipients += L")";
        }

        switch (exec->RepeatBehaviour())
        {
            case WolfExecution::NotSet:
                log::Info(
                    _L_,
                    L"\r\n\tExecuting \"%s\" (repeat behavior not set)%s%s\r\n\r\n",
                    exec->GetKeyword().c_str(),
                    bDebug ? L" (debug=on)" : L"",
                    strRecipients.c_str());
                break;
            case WolfExecution::CreateNew:
                log::Info(_L_, L"\r\n\tExecuting \"%s\"%s\r\n\r\n", exec->GetKeyword().c_str(), strRecipients.c_str());
                break;
            case WolfExecution::Overwrite:

                if (m_pUploadAgent && exec->ShouldUpload())
                {
                    if (m_pUploadAgent->CheckFileUpload(exec->GetOutputFileName()) == S_OK)
                    {
                        log::Info(
                            _L_,
                            L"\r\n\tExecuting \"%s\" (overwriting existing %s)%s%s\r\n\r\n",
                            exec->GetKeyword().c_str(),
                            m_pUploadAgent->GetRemoteFullPath(exec->GetOutputFileName()).c_str(),
                            bDebug ? L" (debug=on)" : L"",
                            strRecipients.c_str());
                    }
                    else
                    {
                        log::Info(
                            _L_,
                            L"\r\n\tExecuting \"%s\"%s%s\r\n\r\n",
                            exec->GetKeyword().c_str(),
                            bDebug ? L" (debug=on)" : L"",
                            strRecipients.c_str());
                    }
                }
                else
                {
                    if (SUCCEEDED(hr = VerifyFileExists(exec->GetOutputFullPath().c_str())))
                    {
                        log::Info(
                            _L_,
                            L"\r\n\tExecuting \"%s\" (overwriting existing %s)%s%s\r\n\r\n",
                            exec->GetKeyword().c_str(),
                            exec->GetOutputFullPath().c_str(),
                            bDebug ? L" (debug=on)" : L"",
                            strRecipients.c_str());
                    }
                    else
                    {
                        log::Info(
                            _L_,
                            L"\r\n\tExecuting \"%s\"%s%s\r\n\r\n",
                            exec->GetKeyword().c_str(),
                            bDebug ? L" (debug=on)" : L"",
                            strRecipients.c_str());
                    }
                }
                break;
            case WolfExecution::Once:

                if (m_pUploadAgent && exec->ShouldUpload())
                {
                    DWORD dwFileSize = 0L;
                    if (m_pUploadAgent->CheckFileUpload(exec->GetOutputFileName(), &dwFileSize) == S_OK)
                    {
                        if (dwFileSize == 0L)
                        {
                            log::Info(
                                _L_,
                                L"\r\n\tExecuting \"%s\" (overwriting previous _empty_ file)%s%s\r\n\r\n",
                                exec->GetKeyword().c_str(),
                                bDebug ? L" (debug=on)" : L"",
                                strRecipients.c_str());
                            break;
                        }
                        else
                        {
                            log::Info(
                                _L_,
                                L"\r\n\tSkipping \"%s\" (file %s already created and not empty)%s%s\r\n\r\n",
                                exec->GetKeyword().c_str(),
                                m_pUploadAgent->GetRemoteFullPath(exec->GetOutputFileName()).c_str(),
                                bDebug ? L" (debug=on)" : L"",
                                strRecipients.c_str());
                            continue;
                        }
                    }
                    else
                    {
                        log::Info(
                            _L_,
                            L"\r\n\tExecuting \"%s\"%s%s\r\n\r\n",
                            exec->GetKeyword().c_str(),
                            bDebug ? L" (debug=on)" : L"",
                            strRecipients.c_str());
                        break;
                    }
                }
                else
                {
                    if (SUCCEEDED(hr = VerifyFileExists(exec->GetOutputFullPath().c_str())))
                    {
                        WIN32_FILE_ATTRIBUTE_DATA data;
                        ZeroMemory(&data, sizeof(WIN32_FILE_ATTRIBUTE_DATA));

                        if (!GetFileAttributesEx(exec->GetOutputFullPath().c_str(), GetFileExInfoStandard, &data))
                        {
                            log::Warning(
                                _L_,
                                hr = HRESULT_FROM_WIN32(GetLastError()),
                                L"Failed to obtain file attributes of %s\r\n",
                                exec->GetOutputFullPath().c_str());
                            log::Info(
                                _L_,
                                L"\r\n\tSkipping \"%s\" (file %s already exists)%s%s\r\n\r\n",
                                exec->GetKeyword().c_str(),
                                exec->GetOutputFullPath().c_str(),
                                bDebug ? L" (debug=on)" : L"",
                                strRecipients.c_str());
                            continue;
                        }
                        else
                        {
                            if (data.nFileSizeHigh == 0L && data.nFileSizeLow == 0L)
                            {
                                log::Info(
                                    _L_,
                                    L"\r\n\tExecuting \"%s\" (overwriting previous _empty_ file)%s%s\r\n\r\n",
                                    exec->GetKeyword().c_str(),
                                    bDebug ? L" (debug=on)" : L"",
                                    strRecipients.c_str());
                                break;
                            }
                            else
                            {
                                log::Info(
                                    _L_,
                                    L"\r\n\tSkipping \"%s\" (file %s already created and not empty)%s%s\r\n\r\n",
                                    exec->GetKeyword().c_str(),
                                    exec->GetOutputFullPath().c_str(),
                                    bDebug ? L" (debug=on)" : L"",
                                    strRecipients.c_str());
                                continue;
                            }
                        }
                    }
                    else
                    {
                        log::Info(
                            _L_,
                            L"\r\n\tExecuting \"%s\"%s%s\r\n\r\n",
                            exec->GetKeyword().c_str(),
                            bDebug ? L" (debug=on)" : L"",
                            strRecipients.c_str());
                    }
                }
                break;
        }

        if (FAILED(hr = exec->CreateArchiveAgent()))
        {
            log::Error(_L_, hr, L"Archive agent creation failed\r\n");
            continue;
        }

        if (FAILED(hr = exec->CreateCommandAgent(config.bChildDebug, config.msRefreshTimer, exec->GetConcurrency())))
        {
            log::Error(_L_, hr, L"Command agent creation failed\r\n");
            exec->CompleteArchive(m_pUploadMessageQueue.get());
            continue;
        }

        try
        {
            // issue commands
            if (FAILED(hr = exec->EnqueueCommands()))
            {
                log::Error(_L_, hr, L"Command enqueue failed\r\n");
                exec->TerminateAllAndComplete();
                exec->CompleteArchive(m_pUploadMessageQueue.get());
                continue;
            }
            // issue last "Done" message
            if (FAILED(hr = exec->CompleteExecution()))
            {
                log::Error(_L_, hr, L"Command execution completion failed\r\n");
                exec->TerminateAllAndComplete();
                exec->CompleteArchive(m_pUploadMessageQueue.get());
                continue;
            }
        }
        catch (...)
        {
            log::Error(_L_, E_FAIL, L"Exception raised, attempting job termination and archive completion...\r\n");
            exec->TerminateAllAndComplete();
            exec->CompleteArchive(m_pUploadMessageQueue.get());
        }

        exec->CompleteArchive(m_pUploadMessageQueue.get());
    }

    if (_L_->IsLoggingToStream())
    {
        _L_->CloseLogToStream();

        if (auto hr = UploadSingleFile(config.Log.FileName, config.Log.Path); FAILED(hr))
        {
            log::Error(_L_, hr, L"Failed to upload log file\r\n");
        }
    }

    if (config.Output.UploadOutput)
    {
        if (FAILED(hr = CompleteUpload()))
        {
            log::Error(_L_, hr, L"Failed to complete upload agent\r\n");
        }
    }

    if (bJobWasModified)
    {
        if (SUCCEEDED(hr = job.BlockBreakAway()))
        {
            log::Info(_L_, L"\r\nINFO: Job has been re-configured to block breakaway\r\n");
        }
        else
        {
            log::Error(_L_, hr, L"\r\nJob failed to be re-configured to block breakaway\r\n");
        }
    }

    if (config.bBeepWhenDone)
    {
        MessageBeep(MB_ICONINFORMATION);
    }
    return S_OK;
}

HRESULT Main::Run_Keywords()
{
    HRESULT hr = E_FAIL;

    for (const auto& exec : m_wolfexecs)
    {
        log::Info(
            _L_,
            L"[%c] Archive: %s (file is %s)\r\n",
            exec->IsOptional() ? L' ' : L'X',
            exec->GetKeyword().c_str(),
            exec->GetArchiveFileName().c_str());

        for (const auto& command : exec->GetCommands())
        {
            log::Info(_L_, L"\t[%c] Command %s\r\n", command->IsOptional() ? L' ' : L'X', command->Keyword().c_str());
        }

        log::Info(_L_, L"\r\n");
    }

    return S_OK;
}
