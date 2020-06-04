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
#include "LogFileWriter.h"
#include "JobObject.h"
#include "StructuredOutputWriter.h"

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
    try
    {
        auto options = std::make_unique<StructuredOutput::JSON::Options>();
        options->Encoding = OutputSpec::Encoding::UTF8;
        options->bPrettyPrint = false;

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

            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            writer->WriteNamed(L"time", ft);

            writer->BeginCollection(L"keys");
            for (const auto& exec : m_wolfexecs)
            {
                if (!exec->IsOptional())
                {
                    writer->Write(exec->GetKeyword().c_str());
                }
            }
            writer->EndCollection(L"keys");

            writer->BeginElement(L"orc_process");
            {
                writer->WriteNamed(L"version", kOrcFileVerStringW);

                std::wstring strProcessBinary;
                SystemDetails::GetProcessBinary(strProcessBinary);
                writer->WriteNamed(L"binary", strProcessBinary.c_str());

                writer->WriteNamed(L"syswow64", SystemDetails::IsWOW64());

                writer->WriteNamed(L"command_line", GetCommandLine());

                writer->WriteNamed(L"output", config.Output.Path.c_str());
                writer->WriteNamed(L"temp", config.TempWorkingDir.Path.c_str());

                writer->BeginElement(L"user");
                {
                    std::wstring strUserName;
                    SystemDetails::WhoAmI(strUserName);
                    writer->WriteNamed(L"username", strUserName.c_str());

                    std::wstring strUserSID;
                    SystemDetails::UserSID(strUserSID);
                    writer->WriteNamed(L"SID", strUserSID.c_str());

                    bool bElevated = false;
                    SystemDetails::AmIElevated(bElevated);
                    writer->WriteNamed(L"elevated", bElevated);

                    std::wstring locale;
                    if (auto hr = SystemDetails::GetUserLocale(locale); SUCCEEDED(hr))
                        writer->WriteNamed(L"locale", locale.c_str());

                    std::wstring language;
                    if (auto hr = SystemDetails::GetUserLanguage(language); SUCCEEDED(hr))
                        writer->WriteNamed(L"language", language.c_str());
                }
                writer->EndElement(L"user");
            }
            writer->EndElement(L"orc_process");

            writer->BeginElement(L"system");
            {
                {
                    std::wstring strComputerName;
                    SystemDetails::GetOrcComputerName(strComputerName);
                    writer->WriteNamed(L"name", strComputerName.c_str());
                }
                {
                    std::wstring strFullComputerName;
                    SystemDetails::GetOrcFullComputerName(strFullComputerName);
                    writer->WriteNamed(L"fullname", strFullComputerName.c_str());
                }
                {
                    std::wstring strSystemType;
                    SystemDetails::GetSystemType(strSystemType);
                    writer->WriteNamed(L"type", strSystemType.c_str());
                }
                {
                    WORD wArch = 0;
                    SystemDetails::GetArchitecture(wArch);
                    switch (wArch)
                    {
                        case PROCESSOR_ARCHITECTURE_INTEL:
                            writer->WriteNamed(L"architecture", L"x86");
                            break;
                        case PROCESSOR_ARCHITECTURE_AMD64:
                            writer->WriteNamed(L"architecture", L"x64");
                            break;
                        default:
                            break;
                    }
                }
                {
                    writer->BeginElement(L"operating_system");
                    {
                        std::wstring strDescr;
                        SystemDetails::GetDescriptionString(strDescr);

                        writer->WriteNamed(L"description", strDescr.c_str());
                    }
                    {
                        auto [major, minor] = SystemDetails::GetOSVersion();
                        auto version = fmt::format(L"{}.{}", major, minor);
                        writer->WriteNamed(L"version", version.c_str());
                    }
                    {
                        TIME_ZONE_INFORMATION tzi;
                        ZeroMemory(&tzi, sizeof(tzi));
                        if (auto active = GetTimeZoneInformation(&tzi); active != TIME_ZONE_ID_INVALID)
                        {
                            writer->BeginElement(L"time_zone");
                            {
                                writer->WriteNamed(L"daylight", tzi.DaylightName);
                                writer->WriteNamed(L"daylight_bias", tzi.DaylightBias);
                                writer->WriteNamed(L"standard", tzi.StandardName);
                                writer->WriteNamed(L"standard_bias", tzi.StandardBias);
                                writer->WriteNamed(L"current_bias", tzi.Bias);
                                switch (active)
                                {
                                    case TIME_ZONE_ID_UNKNOWN:
                                    case TIME_ZONE_ID_STANDARD:
                                        writer->WriteNamed(L"current", L"standard");
                                        break;
                                    case TIME_ZONE_ID_DAYLIGHT:
                                        writer->WriteNamed(L"current", L"daylight");
                                        break;
                                }
                            }
                            writer->EndElement(L"time_zone");
                        }
                    }
                    {
                        std::wstring locale;
                        if (auto hr = SystemDetails::GetUserLocale(locale); SUCCEEDED(hr))
                            writer->WriteNamed(L"locale", locale.c_str());
                    }
                    {
                        std::wstring language;
                        if (auto hr = SystemDetails::GetUserLanguage(language); SUCCEEDED(hr))
                            writer->WriteNamed(L"language", language.c_str());
                    }
                    {
                        auto tags = SystemDetails::GetSystemTags();
                        writer->BeginCollection(L"tags");
                        for (const auto& tag : tags)
                        {
                            writer->Write(tag.c_str());
                        }
                        writer->EndCollection(L"tags");
                    }
                    writer->EndElement(L"operating_system");
                }
                {
                    writer->BeginElement(L"network");
                    {
                        if (const auto& [hr, adapters] = SystemDetails::GetNetworkAdapters(); SUCCEEDED(hr))
                        {
                            writer->BeginCollection(L"adapters");
                            for (const auto& adapter : adapters)
                            {
                                writer->BeginElement(nullptr);
                                {
                                    writer->WriteNamed(L"name", adapter.Name.c_str());
                                    writer->WriteNamed(L"friendly_name", adapter.FriendlyName.c_str());
                                    writer->WriteNamed(L"description", adapter.Description.c_str());
                                    writer->WriteNamed(L"physical", adapter.PhysicalAddress.c_str());

                                    writer->BeginCollection(L"address");
                                    for (const auto& address : adapter.Addresses)
                                    {
                                        if (address.Mode == SystemDetails::AddressMode::MultiCast)
                                            continue;

                                        writer->BeginElement(nullptr);

                                        switch (address.Type)
                                        {
                                            case SystemDetails::AddressType::IPV4:
                                                writer->WriteNamed(L"ipv4", address.Address.c_str());
                                                break;
                                            case SystemDetails::AddressType::IPV6:
                                                writer->WriteNamed(L"ipv6", address.Address.c_str());
                                                break;
                                            default:
                                                writer->WriteNamed(L"other", address.Address.c_str());
                                                break;
                                        }

                                        switch (address.Mode)
                                        {
                                            case SystemDetails::AddressMode::AnyCast:
                                                writer->WriteNamed(L"mode", L"anycast");
                                                break;
                                            case SystemDetails::AddressMode::MultiCast:
                                                writer->WriteNamed(L"mode", L"multicast");
                                                break;
                                            case SystemDetails::AddressMode::UniCast:
                                                writer->WriteNamed(L"mode", L"unicast");
                                                break;
                                            default:
                                                writer->WriteNamed(L"mode", L"other");
                                                break;
                                        }
                                        writer->EndElement(nullptr);
                                    }
                                    writer->EndCollection(L"address");

                                    writer->WriteNamed(L"dns_suffix", adapter.DNSSuffix.c_str());

                                    writer->BeginCollection(L"dns_server");
                                    for (const auto& dns : adapter.DNS)
                                    {
                                        writer->BeginElement(nullptr);
                                        switch (dns.Type)
                                        {
                                            case SystemDetails::AddressType::IPV4:
                                                writer->WriteNamed(L"ipv4", dns.Address.c_str());
                                                break;
                                            case SystemDetails::AddressType::IPV6:
                                                writer->WriteNamed(L"ipv6", dns.Address.c_str());
                                                break;
                                            default:
                                                writer->WriteNamed(L"other", dns.Address.c_str());
                                                break;
                                        }
                                        writer->EndElement(nullptr);
                                    }
                                    writer->EndCollection(L"dns_server");
                                }
                                writer->EndElement(nullptr);
                            }
                            writer->EndCollection(L"adapters");
                        }
                    }
                    writer->EndElement(L"network");
                }
            }
            writer->EndElement(L"system");
        }
        writer->EndElement(L"dfir-orc");

        writer->Close();
    }
    catch (std::exception& e)
    {
        std::cerr << "std::exception during LogFileWrite initialisation" << std::endl;
        std::cerr << "Caught " << e.what() << std::endl;
        std::cerr << "Type " << typeid(e).name() << std::endl;
        return E_ABORT;
    }
    catch (...)
    {
        std::cerr << "Exception during LogFileWrite initialisation" << std::endl;
        return E_ABORT;
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
