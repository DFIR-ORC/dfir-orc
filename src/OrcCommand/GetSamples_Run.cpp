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
#include "LogFileWriter.h"
#include "ConfigFileWriter.h"
#include "TableOutputWriter.h"

#include "ConfigFile_GetThis.h"

#include "CommandExecute.h"
#include "JobObject.h"

#include "MemoryStream.h"
#include "FileStream.h"

#include "TaskTracker.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command::GetSamples;

HRESULT Main::LoadAutoRuns(TaskTracker& tk, LPCWSTR szTempDir)
{
    HRESULT hr = E_FAIL;

    if (config.bLoadAutoruns)
    {
        log::Info(_L_, L"\r\nLoading autoruns file %s...", config.autorunsOutput.Path.c_str());
        if (FAILED(hr = tk.LoadAutoRuns(config.autorunsOutput.Path.c_str())))
        {
            log::Error(_L_, hr, L"Failed to load autoruns\r\n");
            return hr;
        }
        log::Info(_L_, L"Done.\r\n");
    }
    else if (config.bRunAutoruns)
    {
        // Extract and run Autoruns
        auto command = make_shared<CommandExecute>(_L_, L"AutoRuns");

        std::wstring strAutorunsRef;

        if (FAILED(hr = EmbeddedResource::ExtractValue(_L_, L"", L"AUTORUNS", strAutorunsRef)))
        {
            log::Error(_L_, hr, L"Could not find ressource reference to AutoRuns\r\n");
            return hr;
        }

        wstring strAutorunsCmd;
        if (FAILED(
                hr = EmbeddedResource::ExtractToFile(
                    _L_, strAutorunsRef, L"autorunsc.exe", RESSOURCE_READ_EXECUTE_BA, szTempDir, strAutorunsCmd)))
        {
            log::Error(_L_, hr, L"Failed to extract %s to %s\r\n", strAutorunsRef.c_str(), szTempDir);
            return hr;
        }

        command->AddExecutableToRun(strAutorunsCmd);
        command->AddOnCompleteAction(
            make_shared<OnComplete>(OnComplete::Delete, L"autorunsc.exe", strAutorunsCmd, nullptr));
        command->AddArgument(L"/accepteula -h -x -a *", 0L);

        auto pMemStream = std::make_shared<MemoryStream>(_L_);

        if (FAILED(hr = pMemStream->OpenForReadWrite()))
            return hr;

        auto redir_stdout = ProcessRedirect::MakeRedirect(_L_, ProcessRedirect::StdOutput, pMemStream, true);

        redir_stdout->CreatePipe(L"Autoruns_StdOut");

        command->AddRedirection(redir_stdout);

        log::Info(_L_, L"\r\nRunning autoruns utility...");

        JobObject no_job;
        command->Execute(no_job);

        command->WaitCompletion();

        command->CompleteExecution(nullptr);

        log::Info(_L_, L"Done.\r\n");

        if (command->ExitCode() == 0)
        {
            const CBinaryBuffer buffer = pMemStream->GetConstBuffer();
            // buffer may be bigger than actual data... Adjusting...
            size_t dwBufferLen = strnlen_s((LPSTR)buffer.GetData(), buffer.GetCount());

            log::Info(_L_, L"\r\nLoading autoruns data...");
            if (FAILED(hr = tk.LoadAutoRuns(buffer)))
            {
                log::Info(_L_, L"\r\n");
                log::Error(_L_, hr, L"Failed to load autoruns data\r\n");
                return hr;
            }
            log::Info(_L_, L"Done.\r\n");

            if (config.bKeepAutorunsXML)
            {
                auto fs = std::make_shared<FileStream>(_L_);

                if (FAILED(hr = fs->WriteTo(config.autorunsOutput.Path.c_str())))
                {
                    log::Error(_L_, hr, L"\r\nFailed to create file %s\r\n", config.autorunsOutput.Path.c_str());
                }
                else
                {
                    if (FAILED(hr = tk.SaveAutoRuns(fs)))
                    {
                        log::Error(
                            _L_, hr, L"Failed to save autoruns data to %s\r\n", config.autorunsOutput.Path.c_str());
                    }
                }
            }
        }
        else
        {
            log::Error(_L_, E_FAIL, L"\r\nAutoruns failed (exitcode=0x%lx)\r\n", command->ExitCode());
        }
    }

    return S_OK;
}

HRESULT Main::WriteSampleInformation(const std::vector<std::shared_ptr<SampleItem>>& results)
{
    HRESULT hr = S_OK;

    log::Info(_L_, L"\r\nWriting sample information to %s...\r\n", config.sampleinfoOutput.Path.c_str());

    auto pWriter = TableOutput::GetWriter(_L_, config.sampleinfoOutput);

    if (pWriter == nullptr)
    {
        log::Error(
            _L_,
            E_FAIL,
            L"Failed to create writer for sample information to %s\r\n",
            config.sampleinfoOutput.Path.c_str());
        return E_FAIL;
    }

    auto& output = pWriter->GetTableOutput();

    std::for_each(results.begin(), results.end(), [&output](const std::shared_ptr<SampleItem>& item) {
        SystemDetails::WriteComputerName(output);
        output.WriteString(item->FullPath.c_str());
        output.WriteString(item->FileName.c_str());

        static const WCHAR* szAuthenticodeStatus[] = {
            L"ASUndetermined", L"NotSigned", L"SignedVerified", L"SignedNotVerified", L"SignedTampered", NULL};
        output.WriteEnum(item->Status.Authenticode, szAuthenticodeStatus);

        static const WCHAR* szLoadStatus[] = {L"LSUndetermined", L"Loaded", L"LoadedAndUnloaded", NULL};
        output.WriteEnum(item->Status.Loader, szLoadStatus);

        static const WCHAR* szRegisteredStatus[] = {L"RESUndetermined", L"NotRegistered", L"Registered", NULL};
        output.WriteEnum(item->Status.Registry, szRegisteredStatus);

        static const WCHAR* szRunningStatus[] = {L"RUSUndetermined", L"DoesNotRun", L"Runs", NULL};
        output.WriteEnum(item->Status.Running, szRunningStatus);
        output.WriteEndOfLine();
    });

    pWriter->Close();
    log::Info(_L_, L"Done.\r\n");

    return S_OK;
}

HRESULT Main::WriteTimeline(const TaskTracker::TimeLine& timeline)
{
    HRESULT hr = E_FAIL;

    log::Info(_L_, L"\r\nWriting time line to %s...", config.timelineOutput.Path.c_str());

    auto pWriter = TableOutput::GetWriter(_L_, config.timelineOutput);
    if (pWriter == nullptr)
    {
        log::Error(
            _L_,
            E_FAIL,
            L"Failed to create writer for timeline information to %s\r\n",
            config.timelineOutput.Path.c_str());
        return E_FAIL;
    }

    auto& output = pWriter->GetTableOutput();

    std::for_each(timeline.cbegin(), timeline.cend(), [&output](const std::shared_ptr<CausalityItem>& item) {
        SystemDetails::WriteComputerName(output);
        HRESULT hr1 = output.WriteFileTime(item->Time.QuadPart);
        HRESULT hr2 = output.WriteEnum((DWORD)item->Type, szEventType);
        HRESULT hr3 = output.WriteInteger(item->ParentPid);
        HRESULT hr4 = output.WriteInteger(item->Pid);
        if (item->Sample)
        {
            if (!item->Sample->FullPath.empty())
            {
                output.WriteString(item->Sample->FullPath.c_str());
            }
            else if (!item->Sample->FileName.empty())
            {
                output.WriteString(item->Sample->FileName.c_str());
            }
            else
            {
                output.WriteNothing();
            }
        }
        else
        {
            output.WriteNothing();
        }

        output.WriteEndOfLine();
    });

    pWriter->Close();
    log::Info(_L_, L"Done.\r\n");
    return S_OK;
}

HRESULT Main::WriteGetThisConfig(
    const std::wstring& strConfigFile,
    const std::vector<std::shared_ptr<Location>>& locs,
    const std::vector<std::shared_ptr<SampleItem>>& results)
{
    HRESULT hr = E_FAIL;
    // Collect time line related information
    log::Info(_L_, L"\r\nWriting GetThis configuration to %s...", strConfigFile.c_str());

    // Create GetThis config file
    std::vector<std::shared_ptr<SampleItem>> tocollect;

    std::copy_if(
        results.cbegin(),
        results.cend(),
        std::back_inserter(tocollect),
        [](const std::shared_ptr<SampleItem>& item) -> bool { return item->Status.Authenticode != SignedVerified; });

    ConfigFileWriter config_writer(_L_);

    ConfigItem getthisconfig;

    if (FAILED(hr = Orc::Config::GetThis::root(getthisconfig)))
        return hr;

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

    for (const auto& item : tocollect)
    {

        if (item->Location != nullptr)
            item->Location->SetParse(true);

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

    if (FAILED(config_writer.SetLocations(getthisconfig[GETTHIS_LOCATION], locs)))
        return hr;

    for (auto& location_item : getthisconfig[GETTHIS_LOCATION].NodeList)
    {
        location_item.SubItems[CONFIG_VOLUME_ALTITUDE].strData =
            LocationSet::GetStringFromAltitude(config.locs.GetAltitude());
        location_item.SubItems[CONFIG_VOLUME_ALTITUDE].Status = ConfigItem::PRESENT;
    }

    getthisconfig[GETTHIS_LOCATION].Status = ConfigItem::PRESENT;

    getthisconfig.Status = ConfigItem::PRESENT;

    if (FAILED(
            hr = config_writer.WriteConfig(
                strConfigFile.c_str(), L"GetThis configuration file generated by GetSamples", getthisconfig)))
    {
        log::Error(_L_, hr, L"Failed to write GetThis configuration file (file=%s)\r\n", strConfigFile.c_str());
        return hr;
    }
    log::Info(_L_, L"Done.\r\n");
    return S_OK;
}

HRESULT Main::RunGetThis(const std::wstring& strConfigFile, LPCWSTR szTempDir)
{
    HRESULT hr = E_FAIL;

    log::Info(_L_, L"\r\nRunning GetThis utility...");

    // Extract and run GetThis
    auto command = make_shared<CommandExecute>(_L_, L"GetThis");

    wstring strGetThisCmd;
    if (FAILED(
            hr = EmbeddedResource::ExtractToFile(
                _L_, config.getthisRef, config.getthisName, RESSOURCE_READ_EXECUTE_BA, szTempDir, strGetThisCmd)))
    {
        log::Error(_L_, hr, L"Failed to load extract getthis to %s\r\n", szTempDir);
        return hr;
    }

    command->AddExecutableToRun(strGetThisCmd);

    if (!EmbeddedResource::IsSelf(config.getthisRef))
    {
        command->AddOnCompleteAction(
            make_shared<OnComplete>(OnComplete::Delete, config.getthisName, strGetThisCmd, nullptr));
    }

    wstring strConfigArg = config.getthisArgs;

    strConfigArg += L" /Config=\"" + strConfigFile + L"\"";
    if (config.criteriasConfig.Type == OutputSpec::None)
        command->AddOnCompleteAction(
            make_shared<OnComplete>(OnComplete::Delete, L"GetThisConfig.xml", strConfigFile, nullptr));

    command->AddArgument(strConfigArg, 0L);

    JobObject no_job;
    command->Execute(no_job);
    command->WaitCompletion();
    command->CompleteExecution(nullptr);

    log::Info(_L_, L"Done.\r\n");
    return S_OK;
}

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = LoadWinTrust()))
        return hr;
    if (FAILED(hr = LoadPSAPI()))
        return hr;

    // PROCEED WITH REPORT

    TaskTracker tk(_L_);

    tk.InitializeMappings();

    log::Info(_L_, L"\r\nLoading running processes and modules...");
    if (FAILED(hr = tk.LoadRunningTasksAndModules()))
    {
        log::Error(_L_, hr, L"Failed to load running Tasks and Modules\r\n");
        return hr;
    }
    log::Info(_L_, L"Done.\r\n");

    // Loading Autoruns data
    if (FAILED(hr = LoadAutoRuns(tk, config.tmpdirOutput.Path.c_str())))
    {
        log::Error(_L_, hr, L"Failed to load autoruns data\r\n");
        return hr;
    }

    log::Info(_L_, L"\r\nCoalesce data...");
    std::vector<std::shared_ptr<SampleItem>> results;

    if (FAILED(hr = tk.CoalesceResults(results)))
    {
        log::Error(_L_, hr, L"Failed to compile results\r\n");
        return hr;
    }
    log::Info(_L_, L"Done.\r\n");

    if (!config.bNoSigCheck)
    {
        log::Info(_L_, L"\r\nVerifying code signatures (authenticode)...\r\n");
        if (FAILED(hr = tk.CheckSamplesSignature()))
        {
            log::Error(_L_, hr, L"Failed to compile results\r\n");
            return hr;
        }
        log::Info(_L_, L"Verifying code signatures (authenticode)...Done.\r\n");
    }

    if (config.sampleinfoOutput.Type & OutputSpec::Kind::TableFile)
    {
        if (FAILED(hr = WriteSampleInformation(results)))
        {
            log::Error(_L_, hr, L"Failed to write sample information\r\n");
            return hr;
        }
    }

    if (config.timelineOutput.Type & OutputSpec::Kind::TableFile)
    {
        // Collect time line related information
        const TaskTracker::TimeLine& timeline = tk.GetTimeLine();

        if (FAILED(hr = WriteTimeline(timeline)))
        {
            log::Error(_L_, hr, L"Failed to write timeline information\r\n");
            return hr;
        }
    }

    wstring strConfigFile;
    if (config.criteriasConfig.Type == OutputSpec::File)
    {
        strConfigFile = config.criteriasConfig.Path;
    }
    else
    {
        if (FAILED(hr = UtilGetTempFile(NULL, config.tmpdirOutput.Path.c_str(), L".xml", strConfigFile, NULL)))
            return hr;
    }

    if (config.samplesOutput.Type != OutputSpec::None || config.criteriasConfig.Type != OutputSpec::None)
    {
        if (FAILED(hr = WriteGetThisConfig(strConfigFile, tk.GetAltitudeLocations(), results)))
        {
            log::Error(_L_, hr, L"Failed to write gettthis configuration\r\n");
            return hr;
        }
    }

    if (config.samplesOutput.Type != OutputSpec::None)
    {
        if (FAILED(hr = RunGetThis(strConfigFile, config.tmpdirOutput.Path.c_str())))
        {
            log::Error(_L_, hr, L"Failed to run gettthis\r\n");
            return hr;
        }
    }
    return S_OK;
}
