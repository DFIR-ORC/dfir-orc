//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "WolfLauncher.h"

#include <algorithm>
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
#include "Text/Guid.h"
#include "Text/Fmt/ConfigItem.h"
#include "Text/Fmt/Result.h"
#include "Text/Fmt/ByteQuantity.h"
#include "Text/Fmt/Offset.h"
#include "Text/Fmt/Result.h"
#include "Text/Fmt/Threshold.h"
#include "Log/Syslog/Syslog.h"
#include "Log/Syslog/SyslogSink.h"

using namespace Orc::Command;
using namespace Orc::Log;
using namespace Orc;

using namespace Concurrency;

namespace {

constexpr std::wstring_view kInfo = L"Info";

// Rebuild one recorded upload request for the spec it is about to be replayed to.
//
// Every file the run produces is recorded when it is produced, whatever the PRIMARY spec
// (UploadOutputs.front()) makes of it, so a fallback can be handed the same files later. The recorded
// message carries the primary's decisions though, so replaying it as recorded applies them to somebody
// else's destination: a file that fallback excludes is sent to it anyway, and a primary
// operation="move" deletes a local archive the fallback asked to keep with operation="copy".
//
// Returns nullptr when this file is not to be sent to this spec at all: filtered out, operation="noop",
// already delivered to this very entry by an earlier attempt, or no longer on disk.
//
// 'alreadyUploaded' holds the local paths a previous attempt at THIS entry confirmed, so a retry does not
// send them a second time. It is empty when moving to a DIFFERENT entry, which has received nothing.
UploadMessage::Message MakeReplayRequest(
    const Orc::Command::Wolf::PendingUpload& pending,
    const OutputSpec::Upload& spec,
    const std::set<std::wstring>& alreadyUploaded)
{
    const UploadMessage::Message& recorded = pending.request;
    if (!recorded || recorded->GetRequest() != UploadMessage::UploadFile)
    {
        return nullptr;
    }

    // The recorded remote name is the plain file name, which is what the filters match on. A run
    // artifact is never filtered here, because it was not filtered on its way to the primary either.
    if (pending.filtered && !spec.IsFileUploaded(recorded->RemoteName()))
    {
        Log::Debug(L"Upload of '{}' is filtered out by this upload entry; not replayed", recorded->RemoteName());
        return nullptr;
    }

    if (spec.Operation == OutputSpec::UploadOperation::NoOp)
    {
        return nullptr;
    }

    if (alreadyUploaded.find(recorded->LocalName()) != alreadyUploaded.end())
    {
        Log::Debug(L"Upload of '{}' already succeeded at this entry; not replayed", recorded->LocalName());
        return nullptr;
    }

    // Almost always operation="move" having deleted it after an earlier attempt uploaded it. Replaying it
    // could only fail, and since one failure condemns the whole attempt, every later attempt would fail too.
    if (VerifyFileExists(recorded->LocalName().c_str()) != S_OK)
    {
        Log::Debug(L"Not replaying '{}': the local file is gone", recorded->LocalName());
        return nullptr;
    }

    const bool deleteWhenDone = (spec.Operation == OutputSpec::UploadOperation::Move);
    auto replay = UploadMessage::MakeUploadFileRequest(recorded->RemoteName(), recorded->LocalName(), deleteWhenDone);
    if (replay)
    {
        replay->SetKeyword(recorded->Keyword());
    }

    return replay;
}

class FileInformations
{
public:
    bool file_exists = false;
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
    fileInformations.file_exists = true;

    WIN32_FILE_ATTRIBUTE_DATA data;
    ZeroMemory(&data, sizeof(WIN32_FILE_ATTRIBUTE_DATA));
    if (!GetFileAttributesExW(exec.GetOutputFullPath().c_str(), GetFileExInfoStandard, &data))
    {
        Log::Warn(L"Failed to obtain file attributes of '{}' [{}]", exec.GetOutputFullPath(), LastWin32Error());
        return S_OK;
    }

    fileInformations.size = Traits::ByteQuantity((static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow);

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
        return ec;
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
        return ec;
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
        outcome.Recipients().emplace_back(ToUtf8(item->Name), *certificate);
    }
}

// Whether the destination already holds the WHOLE output of a repeat="Once" command set, the only case
// in which there is nothing left to do - neither collecting nor uploading.
//
// A remote file that merely exists proves nothing: a partial transfer leaves one behind too, and calling
// that "done" skips the set and with it every fallback, so the missing tail is never sent anywhere. The
// local archive is the reference to compare against, and it is there whenever a transfer was incomplete:
// a run that sent nothing keeps it, and operation="move" deletes it only after a full send. No local copy
// beside a file on the remote is therefore the signature of a finished run, while a size that differs -
// or one the destination will not state - is not completeness and must not skip anything.
bool IsRemoteOutputComplete(const FileInformations& remote, const FileInformations& local)
{
    if (!remote.file_exists)
    {
        return false;
    }

    if (!local.file_exists)
    {
        return !remote.size.has_value() || remote.size.value() > 0;
    }

    if (!remote.size.has_value() || !local.size.has_value())
    {
        return false;
    }

    return remote.size.value() == local.size.value();
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
        std::optional<uint64_t> fileSize;
        hr = uploadAgent.CheckFileUpload(exec.GetOutputFileName(), fileSize);
        if (FAILED(hr))
        {
            return hr;
        }

        if (hr == S_FALSE)
        {
            fileInformations.file_exists = false;
            return S_OK;
        }

        fileInformations.file_exists = true;
        fileInformations.path = uploadAgent.GetRemoteFullPath(exec.GetOutputFileName());
        if (fileSize)
        {
            fileInformations.size = Traits::ByteQuantity<uint64_t>(*fileSize);
        }

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

void UpdateOutcome(Command::Wolf::Outcome::Outcome& outcome, const GUID& id, HANDLE hCapsule)
{
    outcome.SetId(id);

    {
        std::wstring computerName;
        SystemDetails::GetFullComputerName(computerName);
        outcome.SetComputerNameValue(computerName);
    }

    {
        std::wstring orcComputerName;
        SystemDetails::GetOrcFullComputerName(orcComputerName);
        outcome.SetOrcComputerNameValue(orcComputerName);
    }

    {
        std::wstring orcSystemType;
        SystemDetails::GetOrcSystemType(orcSystemType);
        outcome.SetOrcSystemTypeValue(orcSystemType);
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

    auto capsulePID = SystemDetails::GetParentProcessId();
    if (capsulePID)
    {
        auto& capsule = outcome.GetCapsule();

        auto commandLine = SystemDetails::GetCmdLine(capsulePID.value());
        if (commandLine)
        {
            capsule.SetCommandLineValue(commandLine.value());
        }

        if (hCapsule)
        {
            auto sha1 = GetProcessExecutableHash(hCapsule, CryptoHashStream::Algorithm::SHA1);
            if (sha1)
            {
                capsule.SetSha1(sha1.value());
            }
        }
    }

    TIME_ZONE_INFORMATION tzi;
    ZeroMemory(&tzi, sizeof(tzi));
    if (auto active = GetTimeZoneInformation(&tzi); active != TIME_ZONE_ID_INVALID)
    {
        outcome.SetTimeZoneInformation(tzi);
        outcome.SetTimeZoneDaylight(active == TIME_ZONE_ID_DAYLIGHT);
    }

    auto timeZoneKeyName = SystemDetails::GetTimeZoneKeyNameFromRegistry();
    if (timeZoneKeyName)
    {
        outcome.SetTimeZoneKeyName(*timeZoneKeyName);
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
        Log::Debug(L"Failed to read file '{}' [{}]", path, hr);
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

std::wstring ToSourceString(CommandParameter::ParamKind kind)
{
    switch (kind)
    {
        case CommandParameter::ParamKind::OutFile:
            return L"file";
        case CommandParameter::ParamKind::OutDirectory:
            return L"directory";
        case CommandParameter::ParamKind::StdErr:
            return L"stderr";
        case CommandParameter::ParamKind::StdOut:
            return L"stderr";
        case CommandParameter::ParamKind::StdOutErr:
            return L"stdouterr";
        default:
            return L"<unknown>";
    }
}

template <typename ThresholdT>
bool CheckPhysicalMemoryRequirement(const std::optional<ThresholdT>& threshold)
{
    if (!threshold)
    {
        return true;
    }

    static ByteQuantity<uint64_t> physicalMemorySize(0);

    if (physicalMemorySize == 0)
    {
        auto rv = SystemDetails::GetPhysicalMemoryInstalledSize();
        if (!rv)
        {
            Log::Error("Failed to retrieve physical memory [{}]", rv.error());
            return false;
        }
        else
        {
            physicalMemorySize = *rv;
        }
    }

    auto rv = threshold->Check(physicalMemorySize);
    if (!rv)
    {
        Log::Error("Failed to check threshold [{}]", rv.error());
        return false;
    }

    return *rv;
}

// Whether a per-file success from this entry means the file actually reached the server. BITS answers the
// upload request as soon as the file joins the job and only surfaces the real outcome later through
// IsComplete, so a file it reported as added may never have left the host: skipping it on the very retry
// meant to deliver it would lose it. REST and FileCopy transfer inline, so their answer is the transfer's.
bool ReportsPerFileSuccess(const OutputSpec::Upload& spec)
{
    return spec.Method == OutputSpec::UploadMethod::REST || spec.Method == OutputSpec::UploadMethod::FileCopy;
}

// Wait between two attempts at the same <upload> entry. Announced at Info because this is the one place a
// run deliberately stands still for a long time, and a silent pause at the end of a collection reads as a
// hang. Blocking is fine here: CompleteUpload runs on the main thread once collection is over.
void WaitBeforeRetry(std::chrono::seconds delay, size_t entry, uint32_t nextAttempt, uint32_t attempts)
{
    // Validate() refuses a delay above the cap at embed time; clamp again so a value that reached us
    // some other way cannot overflow the millisecond conversion.
    const uint32_t seconds = static_cast<uint32_t>(
        std::min<int64_t>(delay.count(), static_cast<int64_t>(OutputSpec::Upload::kMaxRetryDelay)));

    Log::Info(
        L"UPLOAD: upload #{} attempt {}/{} failed; retrying in {} second(s)",
        entry + 1,
        nextAttempt - 1,
        attempts,
        seconds);

    Sleep(seconds * 1000);
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

    auto& journal = m_logging.logger().Get(Logger::Facility::kSyslog);
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
                Log::Error(
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

void Orc::Command::Wolf::Main::UploadSingleFile(
    const std::wstring& fileName,
    const std::wstring& filePath,
    UploadSubject subject)
{
    if (VerifyFileExists(filePath.c_str()) != S_OK)
    {
        Log::Debug(L"Not uploading '{}': the file does not exist", filePath);
        return;
    }

    if (config.Output.UploadOutputs.empty())
    {
        return;
    }

    const OutputSpec::Upload& primary = *config.Output.UploadOutputs.front();
    const bool filtered = (subject == UploadSubject::CollectedData);

    // Collected data is what 'include'/'exclude' select, so an archive reaching the upload agents here -
    // a set whose output already existed, uploaded outside the usual path - obeys them like any other.
    // A run artifact carries no such choice and is always sent.
    const bool acceptedByPrimary = (!filtered || primary.IsFileUploaded(fileName));

    // The record is what the run produced, not what the first destination accepted, so it is written
    // whatever the primary decides. A fallback has its own 'operation' and its own filters, applied when
    // the request is replayed to it (see MakeReplayRequest); gating the record on the primary would hide
    // the file from every later destination, including one configured to take everything. Only the live
    // send below obeys the primary.
    const bool deleteWhenDone = (primary.Operation == OutputSpec::UploadOperation::Move);
    auto msg = UploadMessage::MakeUploadFileRequest(fileName, filePath, deleteWhenDone);
    m_pendingUploads.push_back({msg, filtered});

    if (!acceptedByPrimary)
    {
        Log::Debug(L"Upload of '{}' is filtered out by the first upload entry; not sent to it", fileName);
        return;
    }

    if (primary.Operation == OutputSpec::UploadOperation::NoOp)
    {
        return;
    }

    if (m_pUploadMessageQueue)
    {
        Concurrency::send(m_pUploadMessageQueue.get(), msg);
    }
}

HRESULT Main::RestartUploadAgent(
    const OutputSpec::Upload& spec,
    size_t entry,
    bool afterAsyncAttempt,
    const std::set<std::wstring>& alreadyUploaded)
{
    m_pUploadAgent.reset();
    m_pUploadMessageQueue.reset();
    m_pUploadNotification.reset();

    if (afterAsyncAttempt)
    {
        Log::Warn(
            L"UPLOAD: upload #{} follows a failed asynchronous upload; "
            L"the file may no longer be present if the async transfer already succeeded.",
            entry + 1);
    }

    if (HRESULT hr = InitializeUpload(spec); FAILED(hr))
    {
        Log::Error(L"Failed to initialise upload #{}, skipping [{}]", entry + 1, SystemError(hr));
        return hr;
    }

    if (!m_pUploadMessageQueue)
    {
        // InitializeUpload returned S_OK but no queue was created (e.g. NoUpload method). Nothing to do.
        Log::Warn(L"Upload #{}: no upload method available, skipping", entry + 1);
        return E_FAIL;
    }

    // Replay every recorded request, rebuilt for THIS spec: its own operation and its own include/exclude
    // decide what it receives and whether the local file survives.
    for (const auto& pending : m_pendingUploads)
    {
        if (auto replay = MakeReplayRequest(pending, spec, alreadyUploaded); replay)
        {
            Concurrency::send(m_pUploadMessageQueue.get(), replay);
        }
    }

    return S_OK;
}

HRESULT Main::CompleteUpload()
{
    // m_pUploadAgent may be null here if the primary upload spec failed to initialise during Run_Execute()
    // (e.g. the server is unreachable). We must still enter the loop so retries and any configured fallback
    // specs are attempted - bailing out here would defeat the whole point of the fallback list.
    bool anyAsyncAttempted = false;
    bool uploaded = false;

    // Entry #1's first attempt inherits the agent Run_Execute built, unless that initialisation failed and
    // left nothing to inherit: a destination that was already down when the run started is then retried
    // like any other rather than only being fallen back from.
    bool needsFreshAgent = (m_pUploadAgent == nullptr);

    for (size_t i = 0; i < config.Output.UploadOutputs.size() && !uploaded; ++i)
    {
        const OutputSpec::Upload& uploadSpec = *config.Output.UploadOutputs[i];

        // What this entry has already received, so a retry does not send it a second time. Scoped to the
        // entry on purpose: the next destination has none of it.
        std::set<std::wstring> uploadedHere;

        // An asynchronous entry is never retried, whatever it asks for: BITSAgent::IsComplete answers
        // "done, no failure" as soon as the agent is ready to exit, without looking at the jobs, so there
        // is no failure to react to and a further attempt could only re-queue a transfer already running.
        // Validate warns about this at embed time; this is what makes that warning true.
        const bool retryable = (uploadSpec.Mode != OutputSpec::UploadMode::Asynchronous);
        const uint32_t attempts = retryable ? uploadSpec.Retry + 1 : 1;
        uint32_t attemptsMade = 0;

        for (uint32_t attempt = 1; attempt <= attempts; ++attempt)
        {
            if (needsFreshAgent)
            {
                if (attempt > 1)
                {
                    WaitBeforeRetry(uploadSpec.RetryDelay, i, attempt, attempts);
                }

                if (FAILED(RestartUploadAgent(uploadSpec, i, anyAsyncAttempted, uploadedHere)))
                {
                    break;  // this entry cannot run at all; the next one still may
                }
            }

            // Whoever built the agent this attempt used, the next attempt needs one of its own.
            needsFreshAgent = true;

            if (!m_pUploadMessageQueue || !m_pUploadAgent)
            {
                Log::Warn(L"Upload #{}: agent not available, skipping", i + 1);
                break;
            }

            // Only flag an async attempt once we have a live agent that the job is actually handed to: a
            // spec whose agent failed to initialise never touched the file, so a later attempt must not
            // warn that the file "may already be gone".
            if (uploadSpec.Mode == OutputSpec::UploadMode::Asynchronous)
            {
                anyAsyncAttempted = true;
            }

            attemptsMade = attempt;

            // Signal completion and wait for the agent to drain; async agents finish shortly after.
            Concurrency::send(m_pUploadMessageQueue.get(), UploadMessage::MakeCompleteRequest());
            Concurrency::agent::wait(m_pUploadAgent.get());

            // Read the outcome from the agent, which agent::wait has just ordered against, rather than from
            // the notification handler, whose last message may still be queued (see HasNotifiedFailure).
            if (ReportsPerFileSuccess(uploadSpec))
            {
                const std::set<std::wstring>& delivered = m_pUploadAgent->UploadedFiles();
                uploadedHere.insert(delivered.begin(), delivered.end());
            }

            if (!m_pUploadAgent->HasNotifiedFailure())
            {
                Log::Info(L"Upload completed successfully (upload #{}, attempt {}/{})", i + 1, attempt, attempts);
                uploaded = true;
                break;
            }
        }

        if (!uploaded && i + 1 < config.Output.UploadOutputs.size())
        {
            // attemptsMade is 0 when the entry never got as far as a live agent, which is a different
            // story from an attempt that ran and failed.
            if (attemptsMade == 0)
            {
                Log::Warn(L"Upload #{} could not be started, trying next upload...", i + 1);
            }
            else
            {
                Log::Warn(L"Upload #{} failed after {} attempt(s), trying next upload...", i + 1, attemptsMade);
            }
        }
    }

    // The only critical line of the upload. Each failure along the way is an error, because a fallback may
    // still succeed; without this one, a collection that reached no destination at all would end with
    // nothing more severe than an error the loop moved past.
    if (!uploaded)
    {
        Log::Critical(
            L"UPLOAD: no configured upload succeeded ({} configured); the files that were not uploaded remain "
            L"on this host",
            config.Output.UploadOutputs.size());
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
            writer->WriteNamed(L"id", ToStringW(SystemDetails::GetOrcRunId()));

            writer->WriteNamed(L"version", kOrcFileVerStringW);

            std::wstring start;
            SystemDetails::GetTimeStampISO8601(start);
            writer->WriteNamed(L"start", start);

            std::wstring strTimeStamp;
            SystemDetails::GetTimeStamp(strTimeStamp);
            writer->WriteNamed(L"timestamp", strTimeStamp);

            {
                std::wstring computerName;
                SystemDetails::GetOrcFullComputerName(computerName);
                writer->WriteNamed(L"computer_name", computerName);
            }

            {
                std::wstring systemType;
                SystemDetails::GetOrcSystemType(systemType);
                writer->WriteNamed(L"system_type", systemType);
            }

            auto capsule_id = SystemDetails::GetParentProcessId();
            if (capsule_id)
            {
                const wchar_t kCapsule[] = L"capsule";

                writer->BeginElement(kCapsule);

                auto capsule_cmdline = SystemDetails::GetCmdLine(capsule_id.value());
                if (capsule_cmdline)
                {
                    writer->WriteNamed(L"command_line", capsule_cmdline.value().c_str());
                }

                const auto sha1 = GetProcessExecutableHash(capsule_id.value(), CryptoHashStream::Algorithm::SHA1);
                if (sha1)
                {
                    writer->WriteNamed(L"sha1", sha1.value());
                }

                writer->EndElement(kCapsule);
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
                        writer->WriteNamed(
                            L"archive_timeout",
                            std::chrono::duration_cast<std::chrono::seconds>(exec->ArchiveTimeOut()).count());
                        writer->WriteNamed(
                            L"command_timeout",
                            std::chrono::duration_cast<std::chrono::seconds>(exec->CmdTimeOut()).count());

                        if (exec->GetJobRestrictions().ExtendedLimits || exec->GetJobRestrictions().CpuRateControl)
                        {
                            writer->BeginCollection(L"job");
                            writer->BeginCollection(L"limit");

                            const auto& restrictions = exec->GetJobRestrictions();
                            if (restrictions.ExtendedLimits)
                            {
                                auto& limits = restrictions.ExtendedLimits.value();

                                writer->WriteNamed(L"memory", limits.JobMemoryLimit);
                                writer->WriteNamed(L"usertime", limits.BasicLimitInformation.PerJobUserTimeLimit);
                                writer->WriteNamed(L"process_memory", limits.ProcessMemoryLimit);
                                writer->WriteNamed(
                                    L"process_usertime", limits.BasicLimitInformation.PerProcessUserTimeLimit);
                            }

                            if (restrictions.CpuRateControl)
                            {
                                writer->WriteNamed(L"cpu_rate", restrictions.CpuRateControl.value().CpuRate);
                                writer->WriteNamed(L"cpu_weight", restrictions.CpuRateControl.value().Weight);
                            }

                            writer->EndCollection(L"limit");
                            writer->EndCollection(L"job");
                        }

                        writer->BeginCollection(L"commands");
                        for (const auto& command : exec->GetCommands())
                        {
                            if (!command->IsOptional())
                            {
                                writer->BeginElement(nullptr);
                                writer->WriteNamed(L"name", command->Keyword().c_str());

                                if (command->GetTimeout())
                                {
                                    std::chrono::milliseconds timeout = command->GetTimeout().value();
                                    writer->WriteNamed(
                                        L"timeout", std::chrono::duration_cast<std::chrono::seconds>(timeout).count());
                                }

                                writer->BeginCollection(L"output");

                                for (const auto& parameter : command->GetParameters())
                                {
                                    if (parameter.Kind == CommandParameter::OutFile
                                        || parameter.Kind == CommandParameter::OutDirectory
                                        || parameter.Kind == CommandParameter::StdOut
                                        || parameter.Kind == CommandParameter::StdErr
                                        || parameter.Kind == CommandParameter::StdOutErr)
                                    {
                                        writer->BeginElement(nullptr);
                                        writer->WriteNamed(L"name", parameter.Name);
                                        writer->WriteNamed(L"source", ToSourceString(parameter.Kind));
                                        writer->EndElement(nullptr);
                                    }
                                }

                                writer->EndCollection(L"output");
                                writer->EndElement(nullptr);
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
        return E_UNEXPECTED;
    }
    catch (...)
    {
        std::cerr << "Exception during outline creation" << std::endl;
        return E_UNEXPECTED;
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
        UploadSingleFile(config.Outline.FileName, config.Outline.Path, UploadSubject::RunArtifact);
    }

    return S_OK;
}

HRESULT Main::SetLauncherPriority(WolfPriority priority)
{
    switch (priority)
    {
        case WolfPriority::Idle: {
            if (!SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS)
                || !SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE))
            {
                Log::Debug("Failed to set Idle priority, falling back to Low priority [{}]", LastWin32Error());
                return SetLauncherPriority(WolfPriority::Low);
            }
        }
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
    // Do not take any handle on offline mode but always take it otherwise
    if (config.strOfflineLocation.has_value())
    {
        Log::Debug(L"Multiple instances allowed in offline mode");
    }
    else
    {
        m_hInstanceEvent = CreateEventW(nullptr, TRUE, FALSE, m_instanceId.c_str());
        auto err = GetLastError();

        if (!m_hInstanceEvent->IsValid())
        {
            Log::Error(L"Failed CreateEventW for instance mode [{}]", Win32Error(err));
            return E_FAIL;
        }

        if (err == ERROR_ALREADY_EXISTS)
        {
            Log::Debug(L"Multiple instances running (event name: {})", m_instanceId);

            if (!config.bMultipleInstance.has_value() || *config.bMultipleInstance == false)
            {
                Log::Critical(L"Another DFIR-ORC instance is already running");
                return E_FAIL;
            }
        }
    }

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

    auto [outcome, lock] = m_outcome.Get();

    if (m_hCapsule)
    {
        ::UpdateOutcome(outcome, SystemDetails::GetOrcRunId(), *m_hCapsule);
    }

    ::UpdateOutcome(outcome, config.m_Recipients);
    ::UpdateOutcome(outcome, m_standardOutput);
    ::UpdateOutcome(outcome, m_logging);
    ::UpdateOutcomeWithOutline(outcome, config.Outline);

    auto rv = ::DumpOutcome(outcome, config.Outcome);
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

    UploadSingleFile(config.Outcome.FileName, config.Outcome.Path, UploadSubject::RunArtifact);
    return Success<void>();
}

HRESULT Main::Run_Execute()
{
    HRESULT hr = E_FAIL;

    if (!config.Output.UploadOutputs.empty())
    {
        if (FAILED(hr = InitializeUpload(*config.Output.UploadOutputs.front())))
        {
            Log::Error(
                L"Failed to initialize upload #1; files are kept for the fallback uploads, tried at the end of the run "
                L"[{}]",
                SystemError(hr));
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

    BOOST_SCOPE_EXIT(hr)
    {
        SetThreadExecutionState(ES_CONTINUOUS);
    }
    BOOST_SCOPE_EXIT_END;

    if (config.Outline.IsStructuredFile())
    {
        if (auto hr = CreateAndUploadOutline(); FAILED(hr))
        {
            // Critical: the run goes on without an outline, and the cause above was logged as an Error only.
            Log::Critical("Failed to create the outline; the collection continues without it [{}]", SystemError(hr));
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
        m_journal.Print(ToolName(), {}, L"{} ({})", metaName, metaVersion);
    }

    m_journal.Print(ToolName(), {}, L"Version: {}", kOrcVersionStringW);

    uint64_t physicalMemorySize = 0;
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
                parametersNode, L"Use encryption journal", exec->IsChildDebugActive(config.bUseJournalWhenEncrypting));
            PrintValue(parametersNode, L"Debug", exec->IsChildDebugActive(config.bChildDebug));
            PrintValue(parametersNode, L"Repeat behavior", WolfExecution::ToString(exec->RepeatBehaviour()));

            if (exec->DiskFreeSpaceRequirement())
            {
                PrintValue(parametersNode, L"Free disk space", exec->DiskFreeSpaceRequirement());
            }

            if (exec->PhysicalMemoryRequirement())
            {
                PrintValue(parametersNode, L"Physical memory criteria", exec->PhysicalMemoryRequirement());
            }

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

                        m_journal.Print(
                            ToolName(),
                            exec->GetKeyword(),
                            Log::Level::Error,
                            L"Failed to check remote file status: '{}' [{}]",
                            path,
                            SystemError(hr));
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
                // The local archive is read first: it is the reference that tells a complete remote copy
                // from a partial one, and the remote decision below needs both.
                FileInformations local;
                ::GetLocalOutputFileInformations(*exec, local);

                if (exec->ShouldUpload() && m_pUploadAgent)
                {
                    FileInformations remote;
                    hr = ::GetRemoteOutputFileInformations(*exec, *m_pUploadAgent, remote);
                    if (FAILED(hr))
                    {
                        const auto path = m_pUploadAgent->GetRemoteFullPath(exec->GetOutputFileName());
                        Log::Error(L"Failed to check remote file status: '{}' [{}]", path, SystemError(hr));

                        m_journal.Print(
                            ToolName(),
                            exec->GetKeyword(),
                            Log::Level::Error,
                            L"Failed to check remote file status: '{}' [{}]",
                            path,
                            SystemError(hr));
                    }
                    else if (IsRemoteOutputComplete(remote, local))
                    {
                        m_journal.Print(
                            ToolName(),
                            exec->GetKeyword(),
                            Log::Level::Info,
                            "Skipping set because the remote output file is already complete: '{}' (size: {})",
                            remote.path,
                            remote.size);
                        continue;
                    }
                    else if (remote.file_exists)
                    {
                        // Partial: fall through rather than skip, so the archive is offered to the
                        // upload list again - this destination first, then the fallback candidates.
                        m_journal.Print(
                            ToolName(),
                            exec->GetKeyword(),
                            Log::Level::Info,
                            "Remote output file is incomplete, uploading again: '{}' (remote: {}, local: {})",
                            remote.path,
                            remote.size,
                            local.size);
                    }
                }

                if (local.file_exists && (!local.size || *local.size != 0))
                {
                    m_journal.Print(
                        ToolName(),
                        exec->GetKeyword(),
                        Log::Level::Info,
                        "Skipping set because non-empty local output file already exists: '{}' (size: {})",
                        local.path,
                        local.size);

                    // Archive is ready but was not uploaded
                    UploadSingleFile(
                        exec->GetOutputFileName(), exec->GetOutputFullPath(), UploadSubject::CollectedData);
                    continue;
                }
            }

            commandSetNode.AddEmptyLine();
        }

        if (!CheckPhysicalMemoryRequirement(exec->PhysicalMemoryRequirement()))
        {
            m_journal.Print(
                ToolName(),
                exec->GetKeyword(),
                Log::Level::Info,
                "Skipping set because of unmet physical memory requirement (requires: {})",
                exec->PhysicalMemoryRequirement()->ToString());
            continue;
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
        UploadSingleFile(localPath.filename(), localPath, UploadSubject::RunArtifact);
    }

    const auto start = FromSystemTime(theStartTime);
    if (start.has_error())
    {
        Log::Debug(L"Failed to convert start time to time point [{}]", start.error());
        m_journal.Print(ToolName(), {}, L"Done");
    }
    else
    {
        m_journal.Print(ToolName(), {}, L"Done (elapsed: {:%T})", std::chrono::system_clock::now() - *start);
    }

    const auto& fileSink = m_logging.fileSink();
    if (fileSink->IsOpen())
    {
        const auto localPath = fileSink->OutputPath();  // must be done before calling 'Close'

        // The log is closed, sealed and uploaded before the upload completes, so it can never hold the result of
        // that upload. Say so in the file itself, for whoever reads it without the console output.
        if (!config.Output.UploadOutputs.empty())
        {
            Log::Info(
                L"The log file ends here: the upload is completed after it is closed, and its result is reported on "
                L"the console only");
        }

        fileSink->Close();

        if (localPath)
        {
            UploadSingleFile(localPath->filename(), *localPath, UploadSubject::RunArtifact);
        }
    }

    if (!config.Output.UploadOutputs.empty())
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
        exec.CompleteArchive(m_pUploadMessageQueue.get(), m_pendingUploads);
        return hr;
    }

    auto completeArchiveOnExit = Guard::CreateScopeGuard([&]() {
        if (FAILED(hr))
        {
            exec.TerminateAllAndComplete();
        }

        HRESULT hrComplete = exec.CompleteArchive(m_pUploadMessageQueue.get(), m_pendingUploads);
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
