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
#include "FileStream.h"
#include "EmbeddedResource.h"

#include "Temporary.h"

#include "ParameterCheck.h"
#include "SystemDetails.h"

#include "Robustness.h"

#include <array>
#include <boost/algorithm/string.hpp>

#include "Log/Log.h"
#include "Utils/WinApi.h"
#include "Text/Guid.h"
#include "CryptoHashStream.h"

using namespace std;

using namespace Orc;

namespace {

bool GetKeyAndValue(std::wstring_view input, wchar_t separator, std::wstring_view& key, std::wstring_view& value)
{
    const auto separatorPos = input.find_first_of(separator);
    if (separatorPos == std::wstring::npos)
    {
        key = std::wstring_view(input.data(), input.size());
        value = std::wstring_view();
        return true;
    }

    key = std::wstring_view(input.data(), separatorPos);
    value = std::wstring_view(input.data() + separatorPos + 1);
    return true;
}

bool ParseCommandLineArgument(std::wstring_view input, std::wstring_view& key, std::wstring_view& value)
{
    if (input.size() < 1 || (input[0] != '/' && input[0] != '\\'))
    {
        return false;
    }

    return GetKeyAndValue(std::wstring_view(input.data() + 1, input.size() - 1), L'=', key, value);
}

bool ParseCommandLineArgumentValue(std::wstring_view input, std::wstring_view expectedKey, std::wstring& value)
{
    std::vector<std::wstring> arguments;
    boost::split(arguments, input, boost::is_any_of(" "));
    for (const auto& argument : arguments)
    {
        std::wstring_view key;
        std::wstring_view val;
        if (ParseCommandLineArgument(argument, key, val) && key == expectedKey)
        {
            value = val;
            return true;
        }
    }

    return false;
}

// Lookup for '/config' argument to either a file or resource location
std::shared_ptr<ByteStream> OpenCliConfig(std::wstring_view configLocation)
{
    HRESULT hr = E_FAIL;

    if (EmbeddedResource::IsResourceBased(configLocation.data()))
    {
        CBinaryBuffer buffer;
        hr = EmbeddedResource::ExtractToBuffer(configLocation.data(), buffer);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to extract config resource: {} [{}]", configLocation, SystemError(hr));
            return nullptr;
        }

        auto memStream = std::make_shared<MemoryStream>();
        ULONGLONG written;
        hr = memStream->Write(buffer.GetData(), buffer.GetCount(), &written);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to read config file: {} [{}]", configLocation, SystemError(hr));
            return nullptr;
        }

        return memStream;
    }
    else
    {
        auto fileStream = std::make_shared<FileStream>();
        hr = fileStream->OpenFile(
            configLocation.data(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            0);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to read config file: {} [{}]", configLocation, SystemError(hr));
            return nullptr;
        }

        return fileStream;
    }
}

}  // namespace

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

    m_Resources.SetTempDirectory(m_TempDir);

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

    auto stream = std::make_shared<TemporaryStream>();

    if (FAILED(hr = stream->Open(m_TempDir, L"CommandRedirection", 4 * 1024 * 1024)))
    {
        Log::Error(L"Failed to create temporary stream [{}]", SystemError(hr));
        return nullptr;
    }

    switch (output.Kind)
    {
        case CommandParameter::StdOut:
            retval = ProcessRedirect::MakeRedirect(ProcessRedirect::StdOutput, stream, false);
            break;
        case CommandParameter::StdErr:
            retval = ProcessRedirect::MakeRedirect(ProcessRedirect::StdError, stream, false);
            break;
        case CommandParameter::StdOutErr:
            retval = ProcessRedirect::MakeRedirect(
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
            Log::Error("Could not create pipe for process redirection [{}]", SystemError(hr2));
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
    static const std::wregex r_OrcRunId(L"\\{RunId\\}");

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
    SystemDetails::GetOrcSystemType(strSystemType);
    auto s7 = std::regex_replace(s6, r_SystemType, strSystemType);

    wstring strRunID(ToStringW(SystemDetails::GetOrcRunId()));
    auto s8 = std::regex_replace(s7, r_OrcRunId, strRunID);

    std::swap(s8, output);
    return S_OK;
}

HRESULT CommandAgent::ReversePattern(
    const std::wstring& pattern,
    const std::wstring& input,
    std::wstring& systemType,
    std::wstring& fullComputerName,
    std::wstring& computerName,
    std::wstring& timeStamp)
{
    if (pattern.empty())
    {
        return S_OK;
    }

    const std::wregex r_All(L"(\\{FullComputerName\\})|(\\{ComputerName\\})|(\\{TimeStamp\\})|(\\{SystemType\\})");
    const int FullComputerNameIdx = 1, ComputerNameIdx = 2, TimeStampIdx = 3, SystemTypeIdx = 4;

    array<int, 5> PatternIndex = {0, 0, 0, 0, 0};
    {
        auto it = std::wsregex_iterator(begin(pattern), end(pattern), r_All);
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

    wstring strReverseRegEx = pattern;

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
                systemType = inputMatch[PatternIndex[SystemTypeIdx]].str();
            }
        }
        if (PatternIndex[ComputerNameIdx] > 0)
        {
            if (inputMatch[PatternIndex[ComputerNameIdx]].matched)
            {
                computerName = inputMatch[PatternIndex[ComputerNameIdx]].str();
            }
        }
        if (PatternIndex[FullComputerNameIdx] > 0)
        {
            if (inputMatch[PatternIndex[FullComputerNameIdx]].matched)
            {
                fullComputerName = inputMatch[PatternIndex[FullComputerNameIdx]].str();
            }
        }
        if (PatternIndex[TimeStampIdx] > 0)
        {
            if (inputMatch[PatternIndex[TimeStampIdx]].matched)
            {
                timeStamp = inputMatch[PatternIndex[TimeStampIdx]].str();
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
    auto retval = std::make_shared<CommandExecute>(message->Keyword());
    HRESULT hr = S_OK;

    std::optional<std::wstring> outArgument;
    for (const auto& parameter : message->GetParameters())
    {
        std::wstring pattern;
        if (parameter.Kind == CommandParameter::ParamKind::OutFile
            && ParseCommandLineArgumentValue(parameter.Pattern, L"out", pattern))
        {
            std::filesystem::path filename = parameter.Name;
            outArgument = filename.replace_extension().wstring();
            break;
        }
    }

    std::for_each(
        message->GetParameters().cbegin(),
        message->GetParameters().cend(),
        [this, &hr, &message, &outArgument, retval](const CommandParameter& parameter) {
            switch (parameter.Kind)
            {
                case CommandParameter::StdOut:
                case CommandParameter::StdErr:
                case CommandParameter::StdOutErr: {
                    wstring strFileName;

                    if (FAILED(hr = ApplyPattern(parameter.Keyword, L"", L"", strFileName)))
                    {
                        Log::Error(
                            L"Failed to apply pattern on output name '{}' [{}]", parameter.Name, SystemError(hr));
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
                case CommandParameter::OutFile: {
                    wstring strInterpretedName;

                    if (FAILED(hr = GetOutputFile(parameter.Name.c_str(), strInterpretedName)))
                    {
                        Log::Error(L"GetOutputFile failed, skipping file '{}' [{}]", parameter.Name, SystemError(hr));
                        return;
                    }

                    wstring strFileName;

                    if (FAILED(hr = ApplyPattern(strInterpretedName, L"", L"", strFileName)))
                    {
                        Log::Error(L"Failed to apply parttern on '{}' [{}]", parameter.Name, SystemError(hr));
                        return;
                    }

                    wstring strFilePath;

                    WCHAR szTempDir[ORC_MAX_PATH];
                    if (!m_TempDir.empty())
                        wcsncpy_s(szTempDir, m_TempDir.c_str(), m_TempDir.size());
                    else if (FAILED(hr = UtilGetTempDirPath(szTempDir, ORC_MAX_PATH)))
                        return;

                    if (FAILED(hr = UtilGetUniquePath(szTempDir, strFileName.c_str(), strFilePath)))
                    {
                        Log::Error(L"UtilGetUniquePath failed, skipping file '{}' [{}]", strFileName, SystemError(hr));
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
                case CommandParameter::OutTempFile: {
                    // I don't really know if we need those...
                }
                break;
                case CommandParameter::OutDirectory: {
                    wstring strInterpretedName;

                    if (FAILED(hr = GetOutputFile(parameter.Name.c_str(), strInterpretedName)))
                    {
                        Log::Error(
                            L"GetOutputFile failed, skipping directory '{}' [{}]", parameter.Name, SystemError(hr));
                        return;
                    }

                    WCHAR szTempDir[ORC_MAX_PATH];
                    if (!m_TempDir.empty())
                        wcsncpy_s(szTempDir, m_TempDir.c_str(), m_TempDir.size());
                    else if (FAILED(hr = UtilGetTempDirPath(szTempDir, ORC_MAX_PATH)))
                        return;

                    wstring strDirName;

                    if (FAILED(hr = ApplyPattern(strInterpretedName, L"", L"", strDirName)))
                    {
                        Log::Error(
                            L"Failed to apply parttern on output name '{}' [{}]", parameter.Name, SystemError(hr));
                        return;
                    }

                    wstring strDirPath;
                    if (FAILED(hr = UtilGetUniquePath(szTempDir, strDirName.c_str(), strDirPath)))
                    {
                        Log::Error(
                            L"UtilGetUniquePath failed, skipping directory '{}':  [{}]", strDirName, SystemError(hr));
                        return;
                    }

                    if (FAILED(VerifyDirectoryExists(strDirPath.c_str())))
                    {
                        // Directory does not exist prior to execution, create it, then we can delete when cabbed

                        if (!CreateDirectory(strDirPath.c_str(), NULL))
                        {
                            hr = HRESULT_FROM_WIN32(GetLastError());
                            Log::Error(
                                L"Could not create directory, skipping directory '{}' [{}]",
                                strDirPath,
                                SystemError(hr));
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
                case CommandParameter::Argument: {
                    std::error_code ec;
                    auto arguments =
                        ExpandEnvironmentStringsApi(parameter.Keyword.c_str(), parameter.Keyword.size() + 4096, ec);
                    if (ec)
                    {
                        Log::Error(
                            L"Failed to expand environment arguments string for '{}' [{}]", parameter.Keyword, ec);
                        arguments = parameter.Keyword;
                        ec.clear();
                    }

                    // Embed configuration that has been specified with '/config'
                    std::wstring value;
                    if (ParseCommandLineArgumentValue(arguments, L"config", value))
                    {
                        auto cliConfig = ::OpenCliConfig(value);
                        if (cliConfig)
                        {
                            std::wstring configFileName;
                            if (outArgument)
                            {
                                configFileName = *outArgument;
                            }
                            else
                            {
                                configFileName = message->Keyword();
                            }

                            configFileName = configFileName + L"_cli_config.xml";

                            retval->AddOnCompleteAction(make_shared<OnComplete>(
                                OnComplete::ArchiveAndDelete, configFileName, cliConfig, &m_archive));
                        }
                    }

                    if (FAILED(hr = retval->AddArgument(arguments, parameter.OrderId)))
                        return;
                    break;
                }
                case CommandParameter::Executable: {
                    std::optional<std::wstring> executable;

                    if (EmbeddedResource::IsResourceBased(parameter.Name))
                    {
                        wstring extracted;

                        if (FAILED(hr = m_Resources.GetResource(parameter.Name, parameter.Keyword, extracted)))
                        {
                            Log::Error(L"Failed to extract resource '{}' [{}]", parameter.Name, SystemError(hr));
                            return;
                        }

                        retval->SetOriginResourceName(parameter.Name);

                        auto separator = parameter.Name.find(L'|');
                        if (separator != parameter.Name.npos)
                        {
                            retval->SetOriginFriendlyName(parameter.Name.substr(separator + 1));
                        }

                        if (FAILED(hr = retval->AddExecutableToRun(extracted)))
                            return;

                        executable = extracted;
                    }
                    else
                    {
                        if (FAILED(hr = retval->AddExecutableToRun(parameter.Name)))
                            return;

                        if (message->IsSelfOrcExecutable())
                        {
                            retval->SetOriginFriendlyName(L"<self>");
                        }
                        else
                        {
                            std::filesystem::path path(parameter.Name);
                            retval->SetOriginFriendlyName(path.filename());
                        }

                        executable = parameter.Name;
                    }

                    // Calculate the hash here, not ideal but hard to ensure file existency after
                    // PrepareCommandExecute() and having a way to store it for outcome...
                    if (executable)
                    {
                        auto sha1 = Hash(*executable, CryptoHashStream::Algorithm::SHA1);
                        if (sha1.has_error())
                        {
                            Log::Error(
                                L"Failed to compute sha1 for command '{}' [{}]", message->Keyword(), sha1.error());
                        }
                        else
                        {
                            retval->SetExecutableSha1(*sha1);
                        }
                    }

                    retval->SetIsSelfOrcExecutable(message->IsSelfOrcExecutable());
                    retval->SetOrcTool(message->OrcTool());
                }
                break;
                case CommandParameter::InFile: {
                    if (EmbeddedResource::IsResourceBased(parameter.Name))
                    {
                        wstring extracted;
                        if (FAILED(
                                hr = EmbeddedResource::ExtractToFile(
                                    parameter.Name,
                                    parameter.Keyword,
                                    RESSOURCE_READ_EXECUTE_BA,
                                    m_TempDir,
                                    extracted)))
                        {
                            Log::Error(
                                L"Failed to extract resource '{}' from archive [{}]", parameter.Name, SystemError(hr));
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
                        WCHAR inputfile[ORC_MAX_PATH];
                        if (FAILED(hr = ExpandFilePath(parameter.Name.c_str(), inputfile, ORC_MAX_PATH)))
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

    if (message->GetTimeout().has_value())
    {
        retval->m_timeout = *message->GetTimeout();
    }

    if (m_bChildDebug)
    {
        Log::Debug(L"CommandAgent: Configured dump file path '{}'", m_TempDir);
        retval->AddDumpFileDirectory(m_TempDir);
    }

    if (FAILED(hr))
        return nullptr;

    return retval;
}

void CommandAgent::StartCommandExecute(const std::shared_ptr<CommandMessage>& message)
{
    Concurrency::critical_section::scoped_lock s(m_cs);

    std::shared_ptr<CommandExecute> command = nullptr;
    for (const auto& runningCommand : m_RunningCommands)
    {
        if (runningCommand == nullptr)
        {
            continue;
        }

        if (runningCommand->ProcessID() == message->ProcessID())
        {
            command = runningCommand;
        }
    }

    if (command == nullptr)
    {
        Log::Critical(L"Command '{}' resume rejected, command not found", message->Keyword());
        return;
    }

    HRESULT hr = command->ResumeChildProcess();
    if (FAILED(hr))
    {
        Log::Critical(L"Command '{}' resume failed [{}]", message->Keyword(), SystemError(hr));
        TerminateProcess(command->ProcessHandle(), -1);
        return;
    }

    Concurrency::send<CommandNotification::Notification>(
        m_target, CommandNotification::NotifyStarted(command->GetKeyword(), command->ProcessID()));
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
        hr = command->CreateChildProcess(m_Job, m_bWillRequireBreakAway);
        if (FAILED(hr))
        {
            m_MaximumRunningSemaphore.Release();
            command->CompleteExecution();
            return S_OK;
        }

        {
            Concurrency::critical_section::scoped_lock s(m_cs);
            m_RunningCommands.push_back(command);
        }

        if (command->GetTimeout().has_value() && command->GetTimeout()->count() != 0)
        {
            auto timer = std::make_shared<Concurrency::timer<CommandMessage::Message>>(
                (unsigned int)command->GetTimeout()->count(),
                CommandMessage::MakeAbortMessage(command->GetKeyword(), command->ProcessID(), command->ProcessHandle()),
                static_cast<CommandMessage::ITarget*>(&m_cmdAgentBuffer));

            command->SetTimeoutTimer(timer);
            timer->start();
        }

        // Register a callback that will handle process termination (release semaphore, notify, etc...)
        HANDLE hWaitObject = INVALID_HANDLE_VALUE;
        CompletionBlock* pBlockPtr = (CompletionBlock*)Concurrency::Alloc(sizeof(CompletionBlock));
        CompletionBlock* pBlock = new (pBlockPtr) CompletionBlock;
        pBlock->pAgent = this;
        pBlock->command = command;

        if (!RegisterWaitForSingleObject(
                &hWaitObject,
                command->ProcessHandle(),
                WaitOrTimerCallbackFunction,
                pBlock,
                INFINITE,
                WT_EXECUTEDEFAULT | WT_EXECUTEONLYONCE))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Error(L"Could not register for process '{}' termination [{}]", command->GetKeyword(), SystemError(hr));
            return hr;
        }

        auto notification = CommandNotification::NotifyCreated(command->GetKeyword(), command->ProcessID());

        notification->SetOriginFriendlyName(command->GetOriginFriendlyName());
        notification->SetOriginResourceName(command->GetOriginResourceName());
        notification->SetExecutableSha1(command->GetExecutableSha1());
        notification->SetOrcTool(command->GetOrcTool());
        notification->SetIsSelfOrcExecutable(command->IsSelfOrcExecutable());
        notification->SetProcessHandle(command->ProcessHandle());
        notification->SetProcessCommandLine(command->m_commandLine);

        SendResult(notification);
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
                Log::Error(L"Failed GetQueuedCompletionStatus [{}]", Win32Error(err));
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
        Log::Error(L"Failed to create IO Completion port for job object [{}]", m_Keyword, LastWin32Error());
        done();
        return;
    }

    HANDLE hJob = INVALID_HANDLE_VALUE;
    auto hr = JobObject::GetJobObject(GetCurrentProcess(), hJob);

    if (hr == S_OK && hJob != INVALID_HANDLE_VALUE)
    {
        JobObject currentJob = JobObject(hJob);
        if (currentJob.IsValid())
        {
            Log::Debug(L"WolfLauncher is running under an existing job!");
            if (currentJob.IsBreakAwayAllowed())
            {
                Log::Debug(L"Breakaway is allowed, we create our own job and use it!");
                m_Job = JobObject(m_Keyword.c_str());
                m_bWillRequireBreakAway = true;
            }
            else
            {
                // OK, tricky case here... we are in a job, that does not allow breakaway...
                Log::Debug(L"Breakaway is not allowed!");

                // as we'll be hosted inside a "foreign" job or a nested job, limits may or may not apply
                m_bLimitsMUSTApply = false;
                m_bWillRequireBreakAway = false;

                auto [major, minor] = SystemDetails::GetOSVersion();
                if ((major >= 6 && minor >= 2) || major >= 10)
                {
                    Log::Debug(L"Current Windows version allows nested jobs. We create our own job and use it!");
                    m_Job = JobObject(m_Keyword.c_str());
                }
                else
                {
                    Log::Debug(L"Current Windows version does not allow nested jobs. We have to use the current job!");
                    m_Job = std::move(currentJob);
                }
            }
        }
    }
    else if (hr == S_OK && hJob == INVALID_HANDLE_VALUE)
    {
        Log::Debug(L"WolfLauncher is not running under any job!");
        m_Job = JobObject(m_Keyword.c_str());
    }
    else
    {
        Log::Debug(L"WolfLauncher is running under a job we could not determine!");
        JobObject currentJob = JobObject((HANDLE)NULL);
        if (currentJob.IsValid())
        {
            Log::Debug(L"WolfLauncher is running under an existing job!");
            if (currentJob.IsBreakAwayAllowed())
            {
                Log::Debug(L"Breakaway is allowed, we create our own job and use it!");
                m_Job = JobObject(m_Keyword.c_str());
                if (!m_Job.IsValid())
                {
                    Log::Error(L"Failed to create job");
                }
                m_bWillRequireBreakAway = true;
            }
            else
            {
                // OK, tricky case here... we are in a job, don't know which one, that does not allow breakaway...
                Log::Debug(L"Breakaway is not allowed!");

                // as we'll be hosted inside a "foreign" job or a nested job, limits may or may not apply
                m_bLimitsMUSTApply = false;
                m_bWillRequireBreakAway = false;

                auto [major, minor] = SystemDetails::GetOSVersion();
                if ((major >= 6 && minor >= 2) || major >= 10)
                {
                    Log::Debug(L"Current Windows version allows nested jobs. We create our own job and use it!");
                    m_Job = JobObject(m_Keyword.c_str());
                }
                else
                {
                    Log::Error(
                        L"Current Windows version does not allow nested jobs, we cannot determine which job, we cannot "
                        L"break away. We have to fail!");
                    done();
                    return;
                }
            }
        }
    }

    if (!m_Job.IsValid())
    {
        Log::Critical(L"Failed to create '{}' job object [{}]", m_Keyword, LastWin32Error());
        done();
        return;
    }

    if (FAILED(hr = m_Job.AssociateCompletionPort(m_hCompletionPort, this)))
    {
        Log::Critical(L"Failed to associate job object with completion port '{}' [{}]", m_Keyword, SystemError(hr));
        done();
        return;
    }

    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(m_Job.GetHandle(), JobObjectExtendedLimitInformation, &info, sizeof(info)))
        {
            Log::Error("Failed SetInformationJobObject with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE [{}]", LastWin32Error());
        }
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
                hr = HRESULT_FROM_WIN32(GetLastError()),
                Log::Critical("Failed to set basic UI restrictions on job object [{}]", SystemError(hr));
                done();
                return;
            }
            else
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                Log::Warn("Failed to set basic UI restrictions on job object [{}]", SystemError(hr));
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
            Log::Critical("Failed to retrieve end of job behavior on job object [{}]", LastWin32Error());
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
            Log::Warn(
                L"Failed to set end of job behavior (to post at end of job) on job object [{}]", LastWin32Error());
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
                    hr = HRESULT_FROM_WIN32(GetLastError()),
                    Log::Critical("Failed to set CPU Rate limits on job object [{}]", SystemError(hr));
                    done();
                    return;
                }
                else
                {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    Log::Warn(L"Failed to CPU Rate limits on job object [{}]", SystemError(hr));
                }
            }
            else
            {
                Log::Debug(L"CPU rate limits successfully applied");
            }
        }
        else
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Warn(
                L"CPU Rate limit: this Windows version does not support this feature (<6.2) [{}]", SystemError(hr));
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
                hr = HRESULT_FROM_WIN32(GetLastError());
                Log::Critical(L"Failed to set extended limits on job object [{}]", SystemError(hr));
                done();
                return;
            }
            else
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                Log::Warn(L"Failed to set extended limits on job object [{}]", SystemError(hr));
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
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Critical(L"Failed to obtain extended limits on job object [{}]", SystemError(hr));
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
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Critical(L"Failed to set extended limits on job object [{}]", SystemError(hr));
            done();
            return;
        }
    }

    // We initiate the "listening" of the completion port for notifications
    if (!QueueUserWorkItem(JobObjectNotificationRoutine, m_hCompletionPort, WT_EXECUTEDEFAULT))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Critical(L"Failed to queue user work item for completion port [{}]", m_Keyword, SystemError(hr));
        done();
        return;
    }

    while (request)
    {
        switch (request->Request())
        {
            case CommandMessage::Execute: {
                Log::Debug(L"CommandAgent: Execute command '{}'", request->Keyword());
                if (!m_bStopping)
                {
                    auto command = PrepareCommandExecute(request);

                    m_CommandQueue.push(command);
                }
                else
                {
                    Log::Critical(L"Command '{}' rejected, command agent is stopping", request->Keyword());
                }
            }
            break;
            case CommandMessage::Start: {
                Log::Debug(L"CommandAgent: Start command '{}'", request->Keyword());
                StartCommandExecute(request);
            }
            break;
            case CommandMessage::RefreshRunningList: {
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
            case CommandMessage::Terminate: {
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
            case CommandMessage::Abort: {
                Log::Info("Abort process PID: {}", request->ProcessID());

                SendResult(CommandNotification::NotifyAborted(
                    request->Keyword(), request->ProcessID(), request->ProcessHandle()));

                if (!TerminateProcess(request->ProcessHandle(), E_ABORT))
                {
                    Log::Error(L"Failed to terminate process [{}]", LastWin32Error());
                }
            }
            break;
            case CommandMessage::TerminateAll: {
                Log::Critical(L"Kill all running tasks: {}", m_Keyword);

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
                    Log::Error(L"Failed to terminate job object '{}' [{}]", request->Keyword(), SystemError(hr));
                }
                SendResult(CommandNotification::NotifyTerminateAll());
            }
            break;
            case CommandMessage::CancelAnyPendingAndStop: {
                Log::Debug(L"CommandAgent: Cancel any pending task (ie not running) and stop accepting new ones");
                m_bStopping = true;
                {
                    Concurrency::critical_section::scoped_lock lock(m_cs);
                    std::shared_ptr<CommandExecute> cmd;
                    while (m_CommandQueue.try_pop(cmd))
                    {
                        Log::Info(L"Canceling command {}", cmd->GetKeyword());
                    }
                }
                SendResult(CommandNotification::NotifyCanceled());
            }
            break;
            case CommandMessage::QueryRunningList: {
                Log::Debug("CommandAgent: Query running command list");

                Concurrency::critical_section::scoped_lock lock(m_cs);

                std::for_each(
                    m_RunningCommands.cbegin(),
                    m_RunningCommands.cend(),
                    [this](const std::shared_ptr<CommandExecute>& item) {
                        SendResult(CommandNotification::NotifyRunningProcess(item->GetKeyword(), item->ProcessID()));
                    });
            }
            break;
            case CommandMessage::Done: {
                Log::Debug("CommandAgent: Done message received");
                m_bStopping = true;
            }
            break;
            default:
                break;
        }

        if (FAILED(hr = ExecuteNextCommand()))
        {
            Log::Error(L"Failed to execute next command [{}]", SystemError(hr));
        }

        if (m_bStopping && m_RunningCommands.size() == 0 && m_CommandQueue.empty())
        {
            // delete temporary resources
            m_Resources.DeleteTemporaryResources();
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
