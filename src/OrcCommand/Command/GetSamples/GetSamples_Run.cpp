//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "GetSamples.h"

#include "ParameterCheck.h"
#include "SystemDetails.h"
#include "EmbeddedResource.h"
#include "Temporary.h"
#include "Configuration/ConfigFileWriter.h"
#include "TableOutputWriter.h"

#include "Command/GetThis/ConfigFile_GetThis.h"

#include "CommandExecute.h"
#include "JobObject.h"

#include "MemoryStream.h"
#include "FileStream.h"

#include "TaskTracker.h"
#include "Text/Iconv.h"

using namespace Orc::Command::GetSamples;
using namespace Orc;

HRESULT Main::LoadAutoRuns(TaskTracker& tk, LPCWSTR szTempDir)
{
    HRESULT hr = E_FAIL;

    if (config.bLoadAutoruns)
    {
        m_console.Print(L"Loading autoruns file '{}'...", config.autorunsOutput.Path);
        hr = tk.LoadAutoRuns(config.autorunsOutput.Path.c_str());
        if (FAILED(hr))
        {
            Log::Error("Failed to load autoruns [{}]", SystemError(hr));
            return hr;
        }

        Log::Info("Loading autoruns file... Done");
        return S_OK;
    }

    if (config.bRunAutoruns == false)
    {
        return S_OK;
    }

    // Extract and run Autoruns
    auto command = std::make_shared<CommandExecute>(L"AutoRuns");

    std::wstring strAutorunsRef;

    WORD wArch = 0;
    if (FAILED(hr = SystemDetails::GetArchitecture(wArch)))
        return hr;

    switch (wArch)
    {
        case PROCESSOR_ARCHITECTURE_INTEL:
            hr = EmbeddedResource::ExtractValue(L"", L"AUTORUNS", strAutorunsRef);
            if (FAILED(hr))
            {
                Log::Error("Could not find ressource reference to AutoRuns [{}]", SystemError(hr));
                return hr;
            }
            break;
        case PROCESSOR_ARCHITECTURE_AMD64:
            if (SystemDetails::IsWOW64())
            {
                hr = EmbeddedResource::ExtractValue(L"", L"AUTORUNS", strAutorunsRef);
                if (FAILED(hr))
                {
                    Log::Error("Could not find ressource reference to AutoRuns [{}]", SystemError(hr));
                    return hr;
                }
            }
            else
            {
                hr = EmbeddedResource::ExtractValue(L"", L"AUTORUNS64", strAutorunsRef);
                if (FAILED(hr))
                {
                    Log::Info("Could not find ressource reference to AutoRuns x64 fallback to x86 [{}]", SystemError(hr));
                }
                hr = EmbeddedResource::ExtractValue(L"", L"AUTORUNS", strAutorunsRef);
                if (FAILED(hr))
                {
                    Log::Error("Could not find ressource reference to AutoRuns [{}]", SystemError(hr));
                    return hr;
                }
            }
            break;
        default:
            Log::Error("Unsupported architecture: {}", wArch);
            return hr;
    }

    std::wstring strAutorunsCmd;
    hr = EmbeddedResource::ExtractToFile(
        strAutorunsRef, L"autorunsc.exe", RESSOURCE_READ_EXECUTE_BA, szTempDir, strAutorunsCmd);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to extract '{}' to '{}' [{}]", strAutorunsRef, szTempDir, SystemError(hr));
        return hr;
    }

    command->AddExecutableToRun(strAutorunsCmd);
    command->AddOnCompleteAction(
        std::make_shared<OnComplete>(OnComplete::Delete, L"autorunsc.exe", strAutorunsCmd, nullptr));
    command->AddArgument(L"/accepteula -t -h -x -a *", 0L);

    auto pMemStream = std::make_shared<MemoryStream>();
    hr = pMemStream->OpenForReadWrite();
    if (FAILED(hr))
    {
        return hr;
    }

    auto redir_stdout = ProcessRedirect::MakeRedirect(ProcessRedirect::StdOutput, pMemStream, true);
    redir_stdout->CreatePipe(L"Autoruns_StdOut");
    command->AddRedirection(redir_stdout);

    m_console.Print("Running autoruns utility...");
    JobObject no_job;
    command->Execute(no_job);
    command->WaitCompletion();
    command->CompleteExecution(nullptr);
    Log::Info(L"Running autoruns utility... Done");

    if (command->ExitCode() != 0)
    {
        Log::Error(L"Autoruns failed (exit status: {:#x})", command->ExitCode());
        return E_FAIL;
    }

    CBinaryBuffer buffer;

    //
    // Strip from xml anything like version in this autoruns regression:
    //
    // Sysinternals Autoruns v13.51 - Autostart program viewer
    // Copyright (C) 2002-2015 Mark Russinovich
    // Sysinternals - www.sysinternals.com
    //
    {
        const auto output = pMemStream->GetConstBuffer();
        std::wstring_view view(reinterpret_cast<const wchar_t*>(output.GetData()), output.GetCount() / sizeof(wchar_t));

        const auto xmlIt = std::find(std::cbegin(view), std::cend(view), L'<');
        if (xmlIt == std::cend(view))
        {
            Log::Error("Cannot parse autoruns xml");
            return E_INVALIDARG;
        }

        // This will strip also \xFF\xFE but this should not be an issue
        const auto xmlOffset = std::distance(std::cbegin(view), xmlIt);
        buffer.SetData(reinterpret_cast<LPCBYTE>(view.data() + xmlOffset), (view.size() - xmlOffset) * sizeof(wchar_t));
    }

    m_console.Print(L"Loading autoruns data...");
    if (FAILED(hr = tk.LoadAutoRuns(buffer)))
    {
        Log::Error("Failed to load autoruns data [{}]", SystemError(hr));
        return hr;
    }

    Log::Info(L"Loading autoruns data... Done");

    if (config.bKeepAutorunsXML)
    {
        auto fs = std::make_shared<FileStream>();
        hr = fs->WriteTo(config.autorunsOutput.Path.c_str());
        if (FAILED(hr))
        {
            Log::Error(L"Failed to create file '{}' [{}]", config.autorunsOutput.Path, SystemError(hr));
            // Continue execution normally
            return S_OK;
        }

        hr = tk.SaveAutoRuns(fs);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to save autoruns data to '{}' [{}]", config.autorunsOutput.Path, SystemError(hr));
            // Continue execution normally
            return S_OK;
        }
    }

    return S_OK;
}

HRESULT Main::WriteSampleInformation(const std::vector<std::shared_ptr<SampleItem>>& results)
{
    HRESULT hr = S_OK;

    m_console.Print(L"Writing sample information to '{}'...", config.sampleinfoOutput.Path);

    auto pWriter = TableOutput::GetWriter(config.sampleinfoOutput);
    if (pWriter == nullptr)
    {
        Log::Error(L"Failed to create writer for sample information to '{}'", config.sampleinfoOutput.Path);
        return E_FAIL;
    }

    auto& output = *pWriter;

    for (const auto& result : results)
    {
        SystemDetails::WriteComputerName(output);
        output.WriteString(result->FullPath);
        output.WriteString(result->FileName);

        static const WCHAR* szAuthenticodeStatus[] = {
            L"ASUndetermined", L"NotSigned", L"SignedVerified", L"SignedNotVerified", L"SignedTampered", NULL};
        output.WriteEnum(result->Status.Authenticode, szAuthenticodeStatus);

        static const WCHAR* szLoadStatus[] = {L"LSUndetermined", L"Loaded", L"LoadedAndUnloaded", NULL};
        output.WriteEnum(result->Status.Loader, szLoadStatus);

        static const WCHAR* szRegisteredStatus[] = {L"RESUndetermined", L"NotRegistered", L"Registered", NULL};
        output.WriteEnum(result->Status.Registry, szRegisteredStatus);

        static const WCHAR* szRunningStatus[] = {L"RUSUndetermined", L"DoesNotRun", L"Runs", NULL};
        output.WriteEnum(result->Status.Running, szRunningStatus);
        output.WriteEndOfLine();
    }

    pWriter->Close();
    Log::Info(L"Writing sample information... Done");

    return S_OK;
}

HRESULT Main::WriteTimeline(const TaskTracker::TimeLine& timeline)
{
    HRESULT hr = E_FAIL;

    m_console.Print(L"Writing time line to '{}'...", config.timelineOutput.Path);

    auto pWriter = TableOutput::GetWriter(config.timelineOutput);
    if (pWriter == nullptr)
    {
        Log::Error(L"Failed to create writer for timeline information to '{}'", config.timelineOutput.Path);
        return E_FAIL;
    }

    auto& output = *pWriter;

    for (const auto& item : timeline)
    {
        SystemDetails::WriteComputerName(output);
        output.WriteFileTime(item->Time.QuadPart);
        output.WriteEnum((DWORD)item->Type, szEventType);
        output.WriteInteger(item->ParentPid);
        output.WriteInteger(item->Pid);

        if (!item->Sample)
        {
            output.WriteNothing();
            output.WriteEndOfLine();
            continue;
        }

        if (!item->Sample->FullPath.empty())
        {
            output.WriteString(item->Sample->FullPath);
        }
        else if (!item->Sample->FileName.empty())
        {
            output.WriteString(item->Sample->FileName);
        }
        else
        {
            output.WriteNothing();
        }

        output.WriteEndOfLine();
    }

    pWriter->Close();
    Log::Info(L"Writing time line... Done");
    return S_OK;
}

HRESULT Main::WriteGetThisConfig(
    const std::wstring& strConfigFile,
    const std::vector<std::shared_ptr<Location>>& locs,
    const std::vector<std::shared_ptr<SampleItem>>& results)
{
    HRESULT hr = E_FAIL;
    // Collect time line related information
    m_console.Print(L"Writing GetThis configuration to '{}'...", strConfigFile);

    // Create GetThis config file
    std::vector<std::shared_ptr<SampleItem>> tocollect;

    std::copy_if(
        results.cbegin(),
        results.cend(),
        std::back_inserter(tocollect),
        [](const std::shared_ptr<SampleItem>& item) -> bool { return item->Status.Authenticode != SignedVerified; });

    ConfigItem getthisconfig;
    hr = Orc::Config::GetThis::root(getthisconfig);
    if (FAILED(hr))
    {
        return hr;
    }

    ConfigFileWriter config_writer;
    config_writer.SetOutputSpec(getthisconfig[GETTHIS_OUTPUT], config.samplesOutput);

    getthisconfig[GETTHIS_SAMPLES].Status = ConfigItem::PRESENT;

    if (config.limits.dwlMaxBytesTotal != INFINITE)
    {
        getthisconfig[GETTHIS_SAMPLES].SubItems[CONFIG_MAXBYTESTOTAL].strData =
            std::to_wstring(config.limits.dwlMaxBytesTotal);
        getthisconfig[GETTHIS_SAMPLES].SubItems[CONFIG_MAXBYTESTOTAL].Status = ConfigItem::PRESENT;
    }

    if (config.limits.dwlMaxBytesPerSample != INFINITE)
    {
        getthisconfig[GETTHIS_SAMPLES].SubItems[CONFIG_MAXBYTESPERSAMPLE].strData =
            std::to_wstring(config.limits.dwlMaxBytesPerSample);
        getthisconfig[GETTHIS_SAMPLES].SubItems[CONFIG_MAXBYTESPERSAMPLE].Status = ConfigItem::PRESENT;
    }

    if (config.limits.dwMaxSampleCount != INFINITE)
    {
        getthisconfig[GETTHIS_SAMPLES].SubItems[CONFIG_MAXSAMPLECOUNT].strData =
            std::to_wstring(config.limits.dwMaxSampleCount);
        getthisconfig[GETTHIS_SAMPLES].SubItems[CONFIG_MAXSAMPLECOUNT].Status = ConfigItem::PRESENT;
    }

    if (!config.samplesOutput.Path.empty())
    {
        getthisconfig[GETTHIS_OUTPUT].strData = config.samplesOutput.Path;
        getthisconfig[GETTHIS_OUTPUT].Status = ConfigItem::PRESENT;
    }

    if (config.limits.bIgnoreLimits)
    {
        getthisconfig[GETTHIS_NOLIMITS].strData = L"";
        getthisconfig[GETTHIS_NOLIMITS].Status = ConfigItem::PRESENT;
    }

    for (const auto& item : tocollect)
    {
        if (item->Location != nullptr)
        {
            item->Location->SetParse(true);
        }

        getthisconfig[GETTHIS_SAMPLES][CONFIG_SAMPLE].Status = ConfigItem::PRESENT;
        if (!item->Path.empty())
        {
            config_writer.AddSamplePath(getthisconfig[GETTHIS_SAMPLES][CONFIG_SAMPLE], item->Path);
        }
        else if (!item->FileName.empty())
        {
            config_writer.AddSampleName(getthisconfig[GETTHIS_SAMPLES][CONFIG_SAMPLE], item->Path);
        }
    }

    hr = config_writer.SetLocations(getthisconfig[GETTHIS_LOCATION], locs);
    if (FAILED(hr))
    {
        return hr;
    }

    for (auto& location_item : getthisconfig[GETTHIS_LOCATION].NodeList)
    {
        location_item.SubItems[CONFIG_VOLUME_ALTITUDE].strData =
            LocationSet::GetStringFromAltitude(config.locs.GetAltitude());
        location_item.SubItems[CONFIG_VOLUME_ALTITUDE].Status = ConfigItem::PRESENT;
    }

    getthisconfig[GETTHIS_LOCATION].Status = ConfigItem::PRESENT;
    getthisconfig.Status = ConfigItem::PRESENT;

    hr = config_writer.WriteConfig(
        strConfigFile.c_str(), L"GetThis configuration file generated by GetSamples", getthisconfig);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to write GetThis configuration file (file: {})", strConfigFile);
        return hr;
    }

    Log::Info(L"Writing GetThis configuration... Done");
    return S_OK;
}

HRESULT Main::RunGetThis(const std::wstring& strConfigFile, LPCWSTR szTempDir)
{
    HRESULT hr = E_FAIL;

    m_console.Print(L"Running GetThis utility...");

    // Extract and run GetThis
    auto command = std::make_shared<CommandExecute>(L"GetThis");

    std::wstring strGetThisCmd;
    hr = EmbeddedResource::ExtractToFile(
        config.getthisRef, config.getthisName, RESSOURCE_READ_EXECUTE_BA, szTempDir, strGetThisCmd);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to load extract getthis to '{}' [{}]", szTempDir, SystemError(hr));
        return hr;
    }

    command->AddExecutableToRun(strGetThisCmd);

    if (!EmbeddedResource::IsSelf(config.getthisRef))
    {
        command->AddOnCompleteAction(
            std::make_shared<OnComplete>(OnComplete::Delete, config.getthisName, strGetThisCmd, nullptr));
    }

    std::wstring strConfigArg = config.getthisArgs;

    strConfigArg += L" /Config=\"" + strConfigFile + L"\"";
    if (config.getThisConfig.Type == OutputSpec::Kind::None)
    {
        command->AddOnCompleteAction(
            std::make_shared<OnComplete>(OnComplete::Delete, L"GetThisConfig.xml", strConfigFile, nullptr));
    }

    auto logConfig = m_utilitiesConfig.log;
    const auto fileSinkOutput = m_logging.fileSink()->OutputPath();
    if (fileSinkOutput && logConfig.file.path)
    {
        std::error_code ec;

        // Override output log path with the one currently being use to ensure using the same file
        const auto logPath = fmt::format(L"\"{}\"", fileSinkOutput->c_str());
        logConfig.file.path = logPath;

        // Override any disposition to be sure to append
        logConfig.file.disposition = FileDisposition::Append;
    }

    const auto logArguments = UtilitiesLoggerConfiguration::ToCommandLineArguments(logConfig);
    if (logArguments)
    {
        strConfigArg += L" " + *logArguments;
    }

    command->AddArgument(strConfigArg, 0L);

    JobObject no_job;
    command->Execute(no_job);
    command->WaitCompletion();
    command->CompleteExecution(nullptr);

    m_console.Print(L"Running GetThis utility... Done");

    return S_OK;
}

HRESULT Main::Run()
{
    HRESULT hr = LoadWinTrust();
    if (FAILED(hr))
    {
        return hr;
    }

    hr = LoadPSAPI();
    if (FAILED(hr))
    {
        return hr;
    }

    // PROCEED WITH REPORT

    TaskTracker tk;
    tk.InitializeMappings();

    m_console.Print(L"Loading running processes and modules...");

    hr = tk.LoadRunningTasksAndModules();
    if (FAILED(hr))
    {
        Log::Error("Failed to load running Tasks and Modules [{}]", SystemError(hr));
        return hr;
    }

    Log::Info("Loading running processes and modules... Done");

    // Loading Autoruns data
    hr = LoadAutoRuns(tk, config.tmpdirOutput.Path.c_str());
    if (FAILED(hr))
    {
        Log::Error("Failed to load autoruns data [{}]", SystemError(hr));
        return hr;
    }

    m_console.Print(L"Coalesce data...");

    std::vector<std::shared_ptr<SampleItem>> results;

    hr = tk.CoalesceResults(results);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to compile results [{}]", SystemError(hr));
        return hr;
    }

    Log::Info("Coalesce data... Done");

    if (!config.bNoSigCheck)
    {
        m_console.Print(L"Verifying code signatures (authenticode)...");

        hr = tk.CheckSamplesSignature();
        if (FAILED(hr))
        {
            Log::Error(L"Failed to compile results [{}]", SystemError(hr));
            return hr;
        }

        Log::Info(L"Verifying code signatures... Done");
    }

    if (HasFlag(config.sampleinfoOutput.Type, OutputSpec::Kind::TableFile))
    {
        hr = WriteSampleInformation(results);
        if (FAILED(hr))
        {
            Log::Error("Failed to write sample information");
            return hr;
        }
    }

    if (HasFlag(config.timelineOutput.Type, OutputSpec::Kind::TableFile))
    {
        // Collect time line related information
        const TaskTracker::TimeLine& timeline = tk.GetTimeLine();
        hr = WriteTimeline(timeline);
        if (FAILED(hr))
        {
            Log::Error("Failed to write timeline information [{}]", SystemError(hr));
            return hr;
        }
    }

    std::wstring strConfigFile;
    if (config.getThisConfig.Type == OutputSpec::Kind::File)
    {
        strConfigFile = config.getThisConfig.Path;
    }
    else
    {
        hr = UtilGetTempFile(NULL, config.tmpdirOutput.Path.c_str(), L".xml", strConfigFile, NULL);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    if (config.samplesOutput.Type != OutputSpec::Kind::None || config.getThisConfig.Type != OutputSpec::Kind::None)
    {
        hr = WriteGetThisConfig(strConfigFile, tk.GetAltitudeLocations(), results);
        if (FAILED(hr))
        {
            Log::Error("Failed to write getthis configuration [{}]", SystemError(hr));
            return hr;
        }
    }

    if (config.samplesOutput.Type != OutputSpec::Kind::None)
    {
        hr = RunGetThis(strConfigFile, config.tmpdirOutput.Path.c_str());
        if (FAILED(hr))
        {
            Log::Error("Failed to run getthis [{}]", SystemError(hr));
            return hr;
        }
    }

    return S_OK;
}
