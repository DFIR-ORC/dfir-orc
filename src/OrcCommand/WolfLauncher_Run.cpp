//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "WolfLauncher.h"

#include <filesystem>

#include <boost/logic/tribool.hpp>
#include <boost/scope_exit.hpp>
#include <boost/algorithm/string/join.hpp>

#include "Robustness.h"
#include "EmbeddedResource.h"
#include "SystemDetails.h"
#include "JobObject.h"
#include "StructuredOutputWriter.h"
#include "Convert.h"
#include "FileStream.h"
#include "SystemIdentity.h"

#include "Utils/Guard.h"
#include "Utils/TypeTraits.h"
#include "Utils/Time.h"
#include "Utils/WinApi.h"
#include "Output/Text/Fmt/Result.h"
#include "Output/Text/Print/Bool.h"

using namespace Concurrency;

namespace {

struct FileInformations
{
    bool exist;
    std::wstring path;
    std::optional<Orc::Traits::ByteQuantity<uint64_t>> size;
};

HRESULT
GetLocalOutputFileInformations(const Orc::Command::Wolf::WolfExecution& exec, FileInformations& fileInformations)
{
    using namespace Orc;

    HRESULT hr = E_FAIL;

    fileInformations = {};

    hr = VerifyFileExists(exec.GetOutputFullPath().c_str());
    if (FAILED(hr))
    {
        return E_FAIL;
    }

    fileInformations.path = exec.GetOutputFullPath();
    fileInformations.exist = true;

    WIN32_FILE_ATTRIBUTE_DATA data;
    ZeroMemory(&data, sizeof(WIN32_FILE_ATTRIBUTE_DATA));
    if (!GetFileAttributesExW(exec.GetOutputFullPath().c_str(), GetFileExInfoStandard, &data))
    {
        Log::Warn(L"Failed to obtain file attributes of '{}' [{}]", exec.GetOutputFullPath(), LastWin32Error());
        return S_OK;
    }

    fileInformations.size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;

    return S_OK;
}

HRESULT GetRemoteOutputFileInformations(
    const Orc::Command::Wolf::WolfExecution& exec,
    Orc::UploadAgent& uploadAgent,
    FileInformations& fileInformations)
{
    HRESULT hr = E_FAIL;

    fileInformations = {};

    if (exec.ShouldUpload())
    {
        DWORD dwFileSize;
        hr = uploadAgent.CheckFileUpload(exec.GetOutputFileName(), &dwFileSize);
        if (FAILED(hr))
        {
            return E_FAIL;
        }

        fileInformations.exist = true;
        fileInformations.path = uploadAgent.GetRemoteFullPath(exec.GetOutputFileName());
        fileInformations.size = dwFileSize;
        return S_OK;
    }

    return S_OK;
}

}  // namespace

namespace Orc {
namespace Command {
namespace Wolf {

const wchar_t kWolfLauncher[] = L"WolfLauncher";

void Main::Configure(int argc, const wchar_t* argv[])
{
    UtilitiesMain::Configure(argc, argv);

    // As WolfLauncher output is already kind of log journal, let's keep it and avoid Logger to add another timestamp
    auto defaultLogger = m_logging.logger().Get(Logger::Facility::kDefault);
    defaultLogger->SetLevel(m_logging.consoleSink()->Level());
    defaultLogger->SetPattern("%v");
}

std::shared_ptr<WolfExecution::Recipient> Main::GetRecipient(const std::wstring& strName)
{
    const auto it = std::find_if(
        std::cbegin(config.m_Recipients),
        std::cend(config.m_Recipients),
        [&strName](const std::shared_ptr<WolfExecution::Recipient>& item) { return !strName.compare(item->Name); });

    if (it != std::cend(config.m_Recipients))
    {
        return *it;
    }

    return nullptr;
}

HRESULT Main::InitializeUpload(const OutputSpec::Upload& uploadspec)
{
    if (uploadspec.Method == OutputSpec::UploadMethod::NoUpload)
    {
        Log::Debug("UPLOAD: no upload method selected");
        return S_OK;
    }

    auto uploadMessageQueue = std::make_unique<UploadMessage::UnboundedMessageBuffer>();
    auto uploadNotification = std::make_unique<concurrency::call<UploadNotification::Notification>>(
        [this](const UploadNotification::Notification& upload) {
            const std::wstring operation = L"Upload";

            HRESULT hr = upload->GetHResult();
            if (FAILED(hr))
            {
                Log::Critical(
                    L"UPLOAD: Operation for '{}' failed: '{}' [{}]",
                    upload->Source(),
                    upload->Description(),
                    SystemError(upload->GetHResult()));

                m_journal.Print(
                    upload->Keyword(),
                    operation,
                    L"Failed upload for '{}': {} [{}]",
                    upload->Source(),
                    upload->Description(),
                    SystemError(upload->GetHResult()));

                return;
            }

            switch (upload->GetType())
            {
                case UploadNotification::Started:
                    m_journal.Print(upload->Keyword(), operation, L"Start");
                    break;
                case UploadNotification::FileAddition:
                    m_journal.Print(
                        upload->Keyword(), operation, L"Add file: {} ({})", upload->Source(), upload->FileSize());
                    break;
                case UploadNotification::DirectoryAddition:
                    m_journal.Print(upload->Keyword(), operation, L"Add directory: {}", upload->Source());
                    break;
                case UploadNotification::StreamAddition:
                    m_journal.Print(upload->Keyword(), operation, L"Add stream: {}", upload->Source());
                    break;
                case UploadNotification::UploadComplete:
                    m_journal.Print(upload->Keyword(), operation, L"Complete: {}", upload->Destination());
                    break;
                case UploadNotification::JobComplete:
                    Log::Debug(L"Upload job complete: {}", upload->Request()->JobName());
                    break;
            }
        });

    auto uploadAgent =
        UploadAgent::CreateUploadAgent(uploadspec, *uploadMessageQueue, *uploadMessageQueue, *uploadNotification);

    if (uploadAgent == nullptr)
    {
        Log::Critical("Failed to create upload agent");
        return E_FAIL;
    }

    if (!uploadAgent->start())
    {
        Log::Critical("Failed to start upload agent");
        // Agent has reference on queue and notifications so make sure it is cleared before
        uploadAgent.reset();
        return E_FAIL;
    }

    m_pUploadAgent = std::move(uploadAgent);
    m_pUploadMessageQueue = std::move(uploadMessageQueue);
    m_pUploadNotification = std::move(uploadNotification);

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
    if (m_pUploadAgent)
    {
        Concurrency::send(m_pUploadMessageQueue.get(), UploadMessage::MakeCompleteRequest());
        concurrency::agent::wait(m_pUploadAgent.get());
    }

    return S_OK;
}

HRESULT Main::SetWERDontShowUI(DWORD dwNewValue, DWORD& dwPreviousValue)
{
    HKEY hWERKey = nullptr;
    DWORD lasterror = RegOpenKey(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\Windows Error Reporting", &hWERKey);
    if (lasterror != ERROR_SUCCESS)
    {
        HRESULT hr = HRESULT_FROM_WIN32(lasterror);
        Log::Error("Failed to open WER registry key [{}]", SystemError(hr));
        return hr;
    }

    DWORD dwValueSize = sizeof(dwPreviousValue);
    lasterror = RegQueryValueEx(hWERKey, L"DontShowUI", NULL, NULL, (LPBYTE)&dwPreviousValue, &dwValueSize);
    if (lasterror != ERROR_SUCCESS)
    {
        HRESULT hr = HRESULT_FROM_WIN32(lasterror);
        Log::Error("Failed to open WER registry DontShowUI value [{}]", SystemError(hr));
        return hr;
    }

    if (dwPreviousValue != dwNewValue)
    {
        lasterror = RegSetValueEx(hWERKey, L"DontShowUI", NULL, REG_DWORD, (LPBYTE)&dwNewValue, sizeof(dwNewValue));
        if (lasterror != ERROR_SUCCESS)
        {
            HRESULT hr = HRESULT_FROM_WIN32(lasterror);
            Log::Error("Failed to set WER registry DontShowUI value [{}]", SystemError(hr));
            return hr;
        }

        if (dwNewValue)
        {
            Log::Debug("WER user interface is now disabled");
        }
        else
        {
            Log::Debug("WER user interface is now enabled");
        }
    }

    return S_OK;
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

        auto writer = StructuredOutputWriter::GetWriter(config.Outline, std::move(options));
        if (writer == nullptr)
        {
            Log::Error(L"Failed to create writer for outline file {}", config.Outline.Path);
            return E_INVALIDARG;
        }

        writer->WriteNamed(L"version", L"1.0");
        writer->BeginElement(L"dfir-orc");
        {
            writer->WriteNamed(L"version", kOrcFileVerStringW);

            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            writer->WriteNamed(L"time", ft);

            std::wstring strTimeStamp;
            SystemDetails::GetTimeStampISO8601(strTimeStamp);
            writer->WriteNamed(L"timestamp", strTimeStamp);

            auto mothership_id = SystemDetails::GetParentProcessId();
            if (mothership_id)
            {
                auto mothership_cmdline = SystemDetails::GetCmdLine(mothership_id.value());
                if (mothership_cmdline)
                    writer->WriteNamed(L"command", mothership_cmdline.value().c_str());
            }
            writer->WriteNamed(L"output", config.Output.Path.c_str());
            writer->WriteNamed(L"temp", config.TempWorkingDir.Path.c_str());

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
        FileStream fs;

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

        Log::Info(
            L"Outline: {} (took {} seconds, size {} bytes)",
            config.Outline.FileName,
            duration.count() / 10000000,
            outlineSize());
    }

    if (std::filesystem::exists(config.Outline.Path))
    {
        if (auto hr = UploadSingleFile(config.Outline.FileName, config.Outline.Path); FAILED(hr))
        {
            Log::Error(L"Failed to upload outline file [{}]", SystemError(hr));
        }
    }

    return S_OK;
}

HRESULT Main::SetLauncherPriority(WolfPriority priority)
{
    switch (priority)
    {
        case WolfPriority::Low:
            SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            break;
        case WolfPriority::Normal:
            SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
            break;
        case WolfPriority::High:
            SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
            break;
    }

    return S_OK;
}

HRESULT Main::Run()
{
    switch (config.SelectedAction)
    {
        case WolfLauncherAction::Execute:
            return Run_Execute();
        case WolfLauncherAction::Keywords:
            return Run_Keywords();
        default:
            return E_NOTIMPL;
    }

    return S_OK;
}

HRESULT Main::Run_Execute()
{
    HRESULT hr = E_FAIL;

    if (config.Log.Type != OutputSpec::Kind::None)
    {
        std::error_code ec;
        m_logging.fileSink()->Open(config.Log.Path, ec);
        if (ec)
        {
            Log::Error("Failed to create log stream [{}]", ec);
            return ToHRESULT(ec);
        }
    }

    if (config.Output.UploadOutput)
    {
        if (FAILED(hr = InitializeUpload(*config.Output.UploadOutput)))
        {
            Log::Error(L"Failed to initalise upload as requested, no upload will be performed [{}]", SystemError(hr));
        }
    }

    hr = SetDefaultAltitude();
    if (FAILED(hr))
    {
        Log::Warn("Failed to configure default altitude [{}]", SystemError(hr));
    }

    hr = SetLauncherPriority(config.Priority);
    if (FAILED(hr))
    {
        Log::Warn("Failed to configure launcher priority [{}]", SystemError(hr));
    }

    if (config.PowerState != WolfPowerState::Unmodified)
    {
        EXECUTION_STATE previousState =
            SetThreadExecutionState(static_cast<EXECUTION_STATE>(config.PowerState) | ES_CONTINUOUS);
    }

    BOOST_SCOPE_EXIT(hr) { SetThreadExecutionState(ES_CONTINUOUS); }
    BOOST_SCOPE_EXIT_END;

    if (config.Outline.IsStructuredFile())
    {
        if (auto hr = CreateAndUploadOutline(); FAILED(hr))
        {
            Log::Critical("Failed to initalise upload as requested, no upload will be performed [{}]", SystemError(hr));
        }
    }

    auto job = JobObject::GetJobObject(GetCurrentProcess());

    bool bJobWasModified = false;
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
                hr = job.AllowBreakAway();
                if (FAILED(hr))
                {
                    Log::Error("Running within a job that won't allow breakaway, exiting [{}]", SystemError(hr));
                    return E_FAIL;
                }

                bJobWasModified = true;
                Log::Info(L"Running within a job, it has been configured to allow breakaway");
            }
            else
            {
                Log::Debug("WolfLauncher is within a job that won't allow breakaway. Windows 8.x allows nested job");
            }
        }
    }

    boost::logic::tribool initialWERDontShowUIStatus = boost::logic::indeterminate;

    if (config.bWERDontShowUI)
    {
        DWORD dwPreviousValue = 0;

        hr = SetWERDontShowUI(1L, dwPreviousValue);
        if (FAILED(hr))
        {
            Log::Error("Failed to set WERDontShowUIStatus to '{}' [{}]", config.bWERDontShowUI, SystemError(hr));
        }
        else
        {
            m_console.Print("WER user interface is enabled");
        }

        initialWERDontShowUIStatus = dwPreviousValue;
    }

    BOOST_SCOPE_EXIT(&initialWERDontShowUIStatus, this_)
    {
        DWORD dwPreviousValue;

        if (!initialWERDontShowUIStatus)
        {
            // Value mas modified from false to true
            this_->SetWERDontShowUI(0L, dwPreviousValue);
        }
        else if (initialWERDontShowUIStatus)
        {
            // Value was modified from true to false
            this_->SetWERDontShowUI(1L, dwPreviousValue);
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
        {
            Log::Debug(L"Skipping optional command set: '{}'", exec->GetKeyword());
            continue;
        }

        if (m_pConfigStream != nullptr)
        {
            // Rewind the config file stream to enable the config to be "not empty"
            m_pConfigStream->SetFilePointer(0LL, FILE_BEGIN, NULL);
        }

        exec->SetUseJournalWhenEncrypting(config.bUseJournalWhenEncrypting);

        // Print command parameters and eventually skip it
        {
            auto [lock, console] = m_journal.Console();

            console.PrintNewLine();
            auto commandSetNode = console.OutputTree().AddNode("Command set '{}'", exec->GetKeyword());
            auto parametersNode = commandSetNode.AddNode("Parameters");
            PrintValue(
                parametersNode, "UseEncryptionJournal", exec->IsChildDebugActive(config.bUseJournalWhenEncrypting));
            PrintValue(parametersNode, "Debug", exec->IsChildDebugActive(config.bChildDebug));
            PrintValue(parametersNode, "RepeatBehavior", WolfExecution::ToString(exec->RepeatBehaviour()));

            if (exec->RepeatBehaviour() == WolfExecution::Repeat::Overwrite)
            {
                FileInformations info;

                if (exec->ShouldUpload() && m_pUploadAgent)
                {
                    hr = ::GetRemoteOutputFileInformations(*exec, *m_pUploadAgent, info);
                    if (SUCCEEDED(hr))
                    {
                        commandSetNode.Add("Overwriting remote file: '{}' ({})", info.path, info.size);
                    }
                }

                hr = ::GetLocalOutputFileInformations(*exec, info);
                if (SUCCEEDED(hr))
                {
                    commandSetNode.Add("Overwriting local file: '{}' ({})", info.path, info.size);
                }
            }
            else if (exec->RepeatBehaviour() == WolfExecution::Repeat::Once)
            {
                FileInformations info;

                if (exec->ShouldUpload() && m_pUploadAgent)
                {
                    hr = ::GetRemoteOutputFileInformations(*exec, *m_pUploadAgent, info);
                    if (SUCCEEDED(hr) && (!info.size || *info.size != 0))
                    {
                        commandSetNode.Add(
                            "Skipping set because non-empty remote output file already exists: '{}' ({})",
                            info.path,
                            info.size);
                        commandSetNode.AddEmptyLine();
                        continue;
                    }
                }

                hr = ::GetLocalOutputFileInformations(*exec, info);
                if (SUCCEEDED(hr) && (!info.size || *info.size != 0))
                {
                    commandSetNode.Add(
                        "Skipping set because non-empty local output file already exists: '{}' ({})",
                        info.path,
                        info.size);
                    commandSetNode.AddEmptyLine();
                    continue;
                }
            }

            commandSetNode.AddEmptyLine();
        }

        hr = ExecuteKeyword(*exec);
        if (FAILED(hr))
        {
            Log::Critical(L"Failed to execute command set '{}' [{}]", exec->GetKeyword(), SystemError(hr));
            continue;
        }
    }
    const auto& fileSink = m_logging.fileSink();
    if (fileSink->IsOpen())
    {
        fileSink->Close();

        if (auto hr = UploadSingleFile(config.Log.FileName, config.Log.Path); FAILED(hr))
        {
            Log::Error(L"Failed to upload log file [{}]", SystemError(hr));
        }
    }

    if (config.Output.UploadOutput)
    {
        hr = CompleteUpload();
        if (FAILED(hr))
        {
            Log::Error("Failed to complete upload agent [{}]", SystemError(hr));
        }
    }

    if (bJobWasModified)
    {
        hr = job.BlockBreakAway();
        if (SUCCEEDED(hr))
        {
            Log::Info(L"Job has been re-configured to block breakaway");
        }
        else
        {
            Log::Error(L"Job failed to be re-configured to block breakaway [{}]", SystemError(hr));
        }
    }

    if (config.bBeepWhenDone)
    {
        MessageBeep(MB_ICONINFORMATION);
    }

    return S_OK;
}

HRESULT Main::ExecuteKeyword(WolfExecution& exec)
{
    HRESULT hr = exec.CreateArchiveAgent();
    if (FAILED(hr))
    {
        Log::Error("Archive agent creation failed [{}]", SystemError(hr));
        return hr;
    }

    // TODO: This should be moved into the try/except and benefit from RAII cleanup below but 'TerminateAllAndComplete'
    // should not be called multiple times without checking
    hr = exec.CreateCommandAgent(config.bChildDebug, config.msRefreshTimer, exec.GetConcurrency());
    if (FAILED(hr))
    {
        Log::Error("Command agent creation failed [{}]", SystemError(hr));
        exec.CompleteArchive(m_pUploadMessageQueue.get());
        return hr;
    }

    auto completeArchiveOnExit = Guard::CreateScopeGuard([&]() {
        if (FAILED(hr))
        {
            exec.TerminateAllAndComplete();
        }

        HRESULT hrComplete = exec.CompleteArchive(m_pUploadMessageQueue.get());
        if (FAILED(hrComplete))
        {
            Log::Error(L"Failed to complete archive '{}' [{}]", exec.GetOutputFileName(), SystemError(hrComplete));
        }
    });

    try
    {
        hr = exec.EnqueueCommands();
        if (FAILED(hr))
        {
            Log::Error("Command enqueue failed [{}]", SystemError(hr));
            return hr;
        }

        // issue last "Done" message
        hr = exec.CompleteExecution();
        if (FAILED(hr))
        {
            Log::Error("Command execution completion failed [{}]", SystemError(hr));
            return hr;
        }

        return S_OK;
    }
    catch (...)
    {
        Log::Critical("Exception raised, attempting job termination and archive completion...");
        return E_FAIL;
    }
}

HRESULT Main::Run_Keywords()
{
    auto root = m_console.OutputTree();

    for (const auto& exec : m_wolfexecs)
    {
        auto keywordNode = root.AddNode(
            L"[{}] {} ({})", exec->IsOptional() ? L' ' : L'X', exec->GetKeyword(), exec->GetArchiveFileName());

        for (const auto& command : exec->GetCommands())
        {
            keywordNode.Add(L"[{}] {}", command->IsOptional() ? L' ' : L'X', command->Keyword());
        }

        root.AddEmptyLine();
    }

    return S_OK;
}

}  // namespace Wolf
}  // namespace Command
}  // namespace Orc
