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
#include <boost/algorithm/string/classification.hpp>
#include <boost/range/algorithm/remove_if.hpp>

#include "Robustness.h"
#include "EmbeddedResource.h"
#include "SystemDetails.h"
#include "JobObject.h"
#include "StructuredOutputWriter.h"
#include "Convert.h"
#include "FileStream.h"
#include "SystemIdentity.h"
#include "CryptoHashStream.h"

#include "Utils/Guard.h"
#include "Utils/TypeTraits.h"
#include "Utils/Time.h"
#include "Utils/WinApi.h"
#include "Text/Fmt/Result.h"
#include "Text/Print/Bool.h"
#include "Log/Syslog/Syslog.h"
#include "Log/Syslog/SyslogSink.h"

using namespace Orc::Command;
using namespace Orc::Log;
using namespace Orc;

using namespace Concurrency;

namespace {

constexpr std::wstring_view kInfo = L"Info";

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

Orc::Result<std::string> WincryptBinaryToString(std::string_view buffer)
{
    std::string publicKey;
    DWORD publicKeySize = 0;
    if (!CryptBinaryToStringA(
            reinterpret_cast<const unsigned char*>(buffer.data()),
            buffer.size(),
            CRYPT_STRING_BASE64HEADER,
            NULL,
            &publicKeySize))

    {
        std::error_code ec = LastWin32Error();
        Log::Debug("Failed CryptBinaryToStringA while calculating output length [{}]", ec);
        return nullptr;
    }

    publicKey.resize(publicKeySize);
    if (!CryptBinaryToStringA(
            reinterpret_cast<const unsigned char*>(buffer.data()),
            buffer.size(),
            CRYPT_STRING_BASE64HEADER,
            publicKey.data(),
            &publicKeySize))

    {
        std::error_code ec = LastWin32Error();
        Log::Debug("Failed CryptBinaryToStringA while converting binary blob [{}]", ec);
        return nullptr;
    }

    publicKey.erase(boost::remove_if(publicKey, boost::is_any_of("\r\n")), publicKey.end());
    return publicKey;
}

void UpdateOutcome(
    Command::Wolf::Outcome::Outcome& outcome,
    const std::vector<std::shared_ptr<Command::Wolf::WolfExecution::Recipient>>& recipients)

{
    for (auto& item : recipients)
    {
        auto certificate = WincryptBinaryToString(item->Certificate);
        if (!certificate)

        {
            Log::Error(L"Failed to convert Wincrypt binary blob for '{}' [{}]", item->Name, certificate.error());
            continue;
        }
        outcome.Recipients().emplace_back(Utf16ToUtf8(item->Name, "<encoding_error>"), *certificate);
    }
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
        std::optional<DWORD> fileSize;
        hr = uploadAgent.CheckFileUpload(exec.GetOutputFileName(), fileSize);
        if (FAILED(hr))
        {
            return hr;
        }

        fileInformations.exist = true;
        fileInformations.path = uploadAgent.GetRemoteFullPath(exec.GetOutputFileName());
        fileInformations.size = fileSize;
        return S_OK;
    }

    return S_OK;
}

Result<std::wstring> GetCurrentExecutableHash(CryptoHashStream::Algorithm algorithm)
{
    std::error_code ec;
    const auto path = GetModuleFileNameApi(NULL, ec);
    if (ec)
    {
        Log::Debug(L"Failed to get module path [{}]", ec);
        return ec;
    }

    return Hash(path, algorithm);
}

Result<std::wstring> GetProcessExecutableHash(HANDLE hProcess, CryptoHashStream::Algorithm algorithm)
{
    std::error_code ec;
    const auto path = GetModuleFileNameExApi(hProcess, NULL, ec);
    if (ec)
    {
        return ec;
    }

    return Hash(path, algorithm);
}

Result<std::wstring> GetProcessExecutableHash(DWORD dwProcessId, CryptoHashStream::Algorithm algorithm)
{
    Guard::Handle hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwProcessId);
    if (!hProcess)
    {
        const auto ec = LastWin32Error();
        Log::Debug(L"Failed OpenProcess: {} [{}]", dwProcessId, ec);
        return ec;
    }

    return GetProcessExecutableHash(*hProcess, algorithm);
}

void UpdateOutcome(Command::Wolf::Outcome::Outcome& outcome, HANDLE hMothership)
{
    auto&& lock = outcome.Lock();

    {
        std::wstring computerName;
        SystemDetails::GetFullComputerName(computerName);
        outcome.SetComputerNameValue(computerName);
    }

    {
        std::wstring timestampKey;
        HRESULT hr = SystemDetails::GetTimeStamp(timestampKey);
        if (SUCCEEDED(hr))
        {
            outcome.SetTimestampKey(timestampKey);
        }
    }

    const auto timestamp = SystemDetails::GetTimeStamp();
    if (timestamp)
    {
        auto timestampTp = FromSystemTime(timestamp.value());
        if (timestampTp)
        {
            outcome.SetStartingTime(*timestampTp);
        }
    }

    outcome.SetEndingTime(std::chrono::system_clock::now());

    auto mothershipPID = SystemDetails::GetParentProcessId();
    if (mothershipPID)
    {
        auto& mothership = outcome.GetMothership();

        auto commandLine = SystemDetails::GetCmdLine(mothershipPID.value());
        if (commandLine)
        {
            mothership.SetCommandLineValue(commandLine.value());
        }

        if (hMothership)
        {
            auto sha1 = GetProcessExecutableHash(hMothership, CryptoHashStream::Algorithm::SHA1);
            if (sha1)
            {
                mothership.SetSha1(sha1.value());
            }
        }
    }

    auto& wolfLauncher = outcome.GetWolfLauncher();

    const auto sha1 = GetCurrentExecutableHash(CryptoHashStream::Algorithm::SHA1);
    if (sha1.has_error())
    {
        Log::Debug(L"Failed to compute sha256 on current executable [{}]", sha1.error());
    }
    else
    {
        wolfLauncher.SetSha1(sha1.value());
    }

    wolfLauncher.SetVersion(kOrcFileVerString);
    wolfLauncher.SetCommandLineValue(GetCommandLineW());
}
void UpdateOutcome(Command::Wolf::Outcome::Outcome& outcome, StandardOutput& standardOutput)
{
    auto consolePath = standardOutput.FileTee().Path();
    if (consolePath)
    {
        outcome.SetConsoleFileName(consolePath->filename());
    }
}

void UpdateOutcome(Command::Wolf::Outcome::Outcome& outcome, UtilitiesLogger& logging)
{
    const auto& fileSink = logging.fileSink();
    if (fileSink)
    {
        auto logPath = fileSink->OutputPath();
        if (logPath)
        {
            outcome.SetLogFileName(logPath->filename());
        }
    }
}

void UpdateOutcomeWithOutline(Command::Wolf::Outcome::Outcome& outcome, OutputSpec& outline)
{
    if (outline.Type == OutputSpec::Kind::None)
    {
        return;
    }

    std::filesystem::path path(outline.Path);
    outcome.SetOutlineFileName(path.filename());
}

Result<uint64_t> GetFileSize(const std::filesystem::path& path)
{
    FileStream fs;
    HRESULT hr = fs.ReadFrom(path.c_str());
    if (FAILED(hr))
    {
        Log::Debug(L"Failed to read file '{}' [{}]", hr);
        return SystemError(hr);
    }

    return fs.GetSize();
}

Orc::Result<void> DumpOutcome(Command::Wolf::Outcome::Outcome& outcome, const OutputSpec& output)
{
    auto options = std::make_unique<StructuredOutput::JSON::Options>();
    options->Encoding = OutputSpec::Encoding::UTF8;
    options->bPrettyPrint = true;

    auto writer = StructuredOutputWriter::GetWriter(output, std::move(options));
    if (writer == nullptr)
    {
        Log::Error(L"Failed to create writer for outcome file {}", output.Path);
        return SystemError(E_FAIL);
    }

    auto rv = Write(outcome, writer);
    if (rv.has_error())
    {
        Log::Error(L"Failed to write outcome file [{}]", rv);
        return rv;
    }

    HRESULT hr = writer->Close();
    if (FAILED(hr))
    {
        const auto error = SystemError(hr);
        Log::Error(L"Failed to close outcome file [{}]", error);
        return error;
    }

    Log::Debug(L"Written outcome file: '{}' (size: {})", output.Path, ToOptional(GetFileSize(output.Path)));
    return Orc::Success<void>();
}

}  // namespace

namespace Orc {
namespace Command {
namespace Wolf {

const wchar_t kWolfLauncher[] = L"WolfLauncher";

void Main::Configure(int argc, const wchar_t* argv[])
{
    // Underlying spdlog's 'wincolor_sink' will write to console using 'WriteConsoleA' which bypass StandardOutput.
    // Forward console sink output to StandardOutput tee file.
    m_logging.consoleSink()->AddOutput(m_standardOutput.FileTeePtr());

    auto& journal = m_logging.logger().Get(Logger::Facility::kJournal);
    journal->SetLevel(Log::Level::Info);
    journal->Add(m_logging.syslogSink());

    UtilitiesMain::Configure(argc, argv);
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
        writer->BeginElement(L"outline");
        {
            writer->WriteNamed(L"version", kOrcFileVerStringW);

            std::wstring start;
            SystemDetails::GetTimeStampISO8601(start);
            writer->WriteNamed(L"start", start);

            std::wstring strTimeStamp;
            SystemDetails::GetTimeStamp(strTimeStamp);
            writer->WriteNamed(L"timestamp", strTimeStamp);

            auto mothership_id = SystemDetails::GetParentProcessId();
            if (mothership_id)
            {
                auto mothership_cmdline = SystemDetails::GetCmdLine(mothership_id.value());
                if (mothership_cmdline)
                {
                    writer->WriteNamed(L"command", mothership_cmdline.value().c_str());
                }

                const auto sha1 = GetProcessExecutableHash(mothership_id.value(), CryptoHashStream::Algorithm::SHA1);
                if (sha1)
                {
                    writer->WriteNamed(L"sha1", sha1.value());
                }
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

        writer->EndElement(L"outline");
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

    std::error_code ec;
    if (std::filesystem::exists(config.Outline.Path, ec))
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

Orc::Result<void> Main::CreateAndUploadOutcome()
{
    if (config.Outcome.Type == OutputSpec::Kind::None)
    {
        Log::Debug("No outcome file specified");
        return Success<void>();
    }

    ::UpdateOutcome(m_outcome, m_hMothership);
    ::UpdateOutcome(m_outcome, config.m_Recipients);
    ::UpdateOutcome(m_outcome, m_standardOutput);
    ::UpdateOutcome(m_outcome, m_logging);
    ::UpdateOutcomeWithOutline(m_outcome, config.Outline);

    auto rv = ::DumpOutcome(m_outcome, config.Outcome);
    if (rv.has_error())
    {

        Log::Error(L"Failed to dump outcome file [{}]", rv);
        return rv;
    }

    std::error_code ec;
    auto exists = std::filesystem::exists(config.Outcome.Path, ec);
    if (ec)
    {
        Log::Error(L"Failed to upload outcome file [{}]", ec);
        return ec;
    }

    if (!exists)
    {
        const auto ec = std::make_error_code(std::errc::no_such_file_or_directory);
        Log::Error(L"Failed to upload outcome file [{}]", ec);
        return ec;
    }

    HRESULT hr = UploadSingleFile(config.Outcome.FileName, config.Outcome.Path);
    if (FAILED(hr))
    {
        const auto error = SystemError(hr);
        Log::Error(L"Failed to upload outcome file [{}]", error);
        return error;
    }

    return Success<void>();
}

HRESULT Main::Run_Execute()
{
    HRESULT hr = E_FAIL;

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

    const std::wstring metaName(kOrcMetaNameW);
    const std::wstring_view metaVersion(kOrcMetaVersionW);
    if (!metaName.empty() && !metaVersion.empty())
    {
        m_journal.Print(ToolName(), kInfo, L"{} ({})", metaName, metaVersion);
    }

    m_journal.Print(ToolName(), kInfo, L"Version: {}", kOrcVersionStringW);

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
                parametersNode, L"UseEncryptionJournal", exec->IsChildDebugActive(config.bUseJournalWhenEncrypting));
            PrintValue(parametersNode, L"Debug", exec->IsChildDebugActive(config.bChildDebug));
            PrintValue(parametersNode, L"RepeatBehavior", WolfExecution::ToString(exec->RepeatBehaviour()));

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
                    else
                    {
                        const auto path = m_pUploadAgent->GetRemoteFullPath(exec->GetOutputFileName());
                        Log::Error(L"Failed to check remote file status: '{}' [{}]", path, SystemError(hr));
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
                    if (FAILED(hr))
                    {
                        const auto path = m_pUploadAgent->GetRemoteFullPath(exec->GetOutputFileName());
                        Log::Error(L"Failed to check remote file status: '{}' [{}]", path, SystemError(hr));
                    }
                    else if (!info.size || *info.size != 0)
                    {
                        commandSetNode.Add(
                            "Skipping set because non-empty remote output file already exists: '{}' (size: {})",
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

                    // Archive is ready but was not uploaded
                    hr = UploadSingleFile(exec->GetOutputFileName(), exec->GetOutputFullPath());
                    if (FAILED(hr))
                    {
                        Log::Error(L"Failed to upload archive '{}' [{}]", exec->GetOutputFullPath(), SystemError(hr));
                    }

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

    auto rv = CreateAndUploadOutcome();
    if (rv.has_error())
    {
        Log::Error(L"Failed to upload outcome [{}]", rv);
    }

    if (m_standardOutput.FileTee().Path().has_value())
    {
        std::error_code ec;
        m_standardOutput.Flush(ec);
        if (ec)
        {
            Log::Error("Failed to flush standard output [{}]", ec);
        }

        m_standardOutput.FileTee().Close(ec);

        const std::filesystem::path localPath = *m_standardOutput.FileTee().Path();
        hr = UploadSingleFile(localPath.filename(), localPath);
        if (FAILED(hr))
        {
            Log::Error(
                L"Failed to upload console log file '{}' [{}]", m_standardOutput.FileTee().Path(), SystemError(hr));
        }
    }

    const auto& fileSink = m_logging.fileSink();
    if (fileSink->IsOpen())
    {
        const auto localPath = fileSink->OutputPath();  // must be done before calling 'Close'
        fileSink->Close();

        if (localPath)
        {
            if (auto hr = UploadSingleFile(localPath->filename(), *localPath); FAILED(hr))
            {
                Log::Error(L"Failed to upload log file [{}]", SystemError(hr));
            }
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

    const auto start = FromSystemTime(theStartTime);
    if (start.has_error())
    {
        Log::Debug(L"Failed to convert start time to time point [{}]", start.error());
        m_journal.Print(ToolName(), kInfo, L"Done");
    }
    else
    {
        m_journal.Print(ToolName(), kInfo, L"Done (elapsed: {:%T})", std::chrono::system_clock::now() - *start);
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
