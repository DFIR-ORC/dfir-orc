//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "Mothership.h"

#include "WMIUtil.h"

#include "Robustness.h"

#include "SystemDetails.h"
#include "EmbeddedResource.h"
#include "Kernel32Extension.h"
#include "ParameterCheck.h"

#include "JobObject.h"

#include "DownloadTask.h"

#include <sstream>

using namespace Orc;
using namespace Orc::Command::Mothership;

constexpr auto MAX_CMDLINE = 32768;

class MotherShipTerminate : public TerminationHandler
{
private:
    HANDLE m_hProcess;
    DWORD m_dwTimeout;
    std::wstring m_FileToDelete;

public:
    MotherShipTerminate(const std::wstring& file, HANDLE hProcess, DWORD dwTimeOut)
        : TerminationHandler(L"Mothership handler", 0)
        , m_FileToDelete(file)
        , m_hProcess(hProcess)
        , m_dwTimeout(dwTimeOut) {};
    HRESULT operator()()
    {
        WaitForSingleObject(m_hProcess, m_dwTimeout);

        bool bDeleted = false;
        DWORD dwRetries = 0;

        do
        {
            if (!DeleteFile(m_FileToDelete.c_str()))
            {
                if (dwRetries > 20)
                {
                    wprintf_s(
                        L"ERROR: Could not delete file \"%s\" (hr=0x%lx)",
                        m_FileToDelete.c_str(),
                        HRESULT_FROM_WIN32(GetLastError()));
                    return HRESULT_FROM_WIN32(GetLastError());
                }
                else
                {
                    wprintf_s(
                        L"VERBOSE: Could not delete file \"%s\" (hr=0x%lx,retries=%d)",
                        m_FileToDelete.c_str(),
                        HRESULT_FROM_WIN32(GetLastError()),
                        dwRetries);
                    dwRetries++;
                    Sleep(500);
                }
            }
            else
            {
                bDeleted = true;
            }

        } while (!bDeleted);

        return S_OK;
    };
};

void Main::Configure(int argc, const wchar_t* argv[])
{
    UtilitiesMain::Configure(argc, argv);
}

HRESULT Main::ChangeTemporaryEnvironment()
{
    HRESULT hr = E_FAIL;

    if (config.Temporary.Path.empty())
        return S_OK;

    if (!SetEnvironmentVariableW(L"TEMP", config.Temporary.Path.c_str()))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed to set %TEMP% to '{}' [{}]", config.Temporary.Path, SystemError(hr));
        return E_INVALIDARG;
    }
    if (!SetEnvironmentVariableW(L"TMP", config.Temporary.Path.c_str()))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed to set %TMP% to '{}' [{}]", config.Temporary.Path, SystemError(hr));
        return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT Main::Launch(const std::wstring& command, const std::wstring& commandArgs)
{
    HRESULT hr = E_FAIL;

    Log::Debug(L"Running '{}'", command);

    std::wstringstream cmdLineBuilder;

    bool bHasDeletionWork = false;

    if (m_DownloadTask)
    {
        if (std::find_if(
                begin(m_DownloadTask->Files()),
                end(m_DownloadTask->Files()),
                [](const DownloadTask::DownloadFile& file) -> bool { return file.bDelete == true; })
            != end(m_DownloadTask->Files()))
            bHasDeletionWork = true;
    }

    std::wstring strCreateProcessBinary;

    if (config.bNoWait && bHasDeletionWork)
    {
        hr = ExpandFilePath(L"%ComSpec%", strCreateProcessBinary);
        if (FAILED(hr))
        {
            Log::Error("Failed to evaluate command spec '%ComSpec%' [{}]", SystemError(hr));
            return hr;
        }

        bool bFirst = true;
        cmdLineBuilder << L"\"" << strCreateProcessBinary << L"\" /Q /E:OFF /D /C";

        if (!command.empty())
        {
            cmdLineBuilder << command;
            cmdLineBuilder << config.strCmdLineArgs;
            bFirst = false;
        }

        for (const auto& file : m_DownloadTask->Files())
        {
            if (file.bDelete)
            {
                if (bFirst)
                {
                    cmdLineBuilder << L"del \"" << file.strLocalPath << "\"";
                    bFirst = false;
                }
                else
                {
                    cmdLineBuilder << L" && del \"" << file.strLocalPath << "\"";
                    bFirst = false;
                }
            }
        }
    }
    else
    {
        strCreateProcessBinary = command;
        cmdLineBuilder << L"\"" << command << L"\" " << commandArgs;
        cmdLineBuilder << config.strCmdLineArgs;
    }

    if (cmdLineBuilder.width() > MAX_CMDLINE)
    {
        Log::Error(L"Command line too long (length: {}): {}", cmdLineBuilder.width(), cmdLineBuilder.str());
        return E_INVALIDARG;
    }

    std::vector<WCHAR> szCommandLine(MAX_CMDLINE);
    std::wstring strCommandLine = cmdLineBuilder.str();

    HANDLE hMothership = OpenProcess(PROCESS_QUERY_INFORMATION, TRUE, GetCurrentProcessId());
    if (hMothership)
    {
        strCommandLine.append(fmt::format(L" /Mothership={:#x}", reinterpret_cast<size_t>(hMothership)));
    }
    else
    {
        Log::Error("Failed to append mothership handle on the cli [{}]", LastWin32Error());
    }

    wcsncpy_s(szCommandLine.data(), MAX_CMDLINE, strCommandLine.c_str(), strCommandLine.size());

    DWORD dwMajor = 0, dwMinor = 0;
    SystemDetails::GetOSVersion(dwMajor, dwMinor);

    HANDLE hParent = INVALID_HANDLE_VALUE;

    if (dwMajor >= 6 && config.dwParentID != 0)
    {
        const auto pk32 = ExtensionLibrary::GetLibrary<Kernel32Extension>();
        if (pk32 == nullptr)
        {
            Log::Error("Failed to obtain the Kernel32.dll as an extension library");
            return E_FAIL;
        }

        SIZE_T sizeToAlloc = 0;
        hr = pk32->InitializeProcThreadAttributeList(nullptr, 1, 0, &sizeToAlloc);
        if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))
        {
            Log::Error("Failed to obtain tread attribute list size [{}]", SystemError(hr));
            return hr;
        }

        auto pAttrList = (PPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeToAlloc);
        if (pAttrList == nullptr)
        {
            return E_OUTOFMEMORY;
        }

        hr = pk32->InitializeProcThreadAttributeList(pAttrList, 1, 0, &sizeToAlloc);
        if (FAILED(hr))
        {
            Log::Error("Failed to obtain tread attribute list [{}]", SystemError(hr));
            return hr;
        }

        m_hParentProcess = OpenProcess(PROCESS_CREATE_PROCESS, FALSE, config.dwParentID);
        if (m_hParentProcess == NULL)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Error(
                "Failed OpenProcess on parent pid {} with PROCESS_CREATE_PROCESS [{}]",
                config.dwParentID,
                SystemError(hr));
            return hr;
        }

        hr = pk32->UpdateProcThreadAttribute(
            pAttrList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &m_hParentProcess, sizeof(hParent), nullptr, nullptr);
        if (FAILED(hr))
        {
            Log::Error("Failed to add process handle to attribute list [{}]", SystemError(hr));
            return hr;
        }

        m_si.StartupInfo.cb = sizeof(STARTUPINFOEX);
        m_si.lpAttributeList = pAttrList;
        config.dwCreationFlags |= EXTENDED_STARTUPINFO_PRESENT;
    }

    config.dwCreationFlags |= CREATE_UNICODE_ENVIRONMENT;

    Log::Debug(L"Starting '{}' with command line '{}'", command, szCommandLine.data());

    if (!CreateProcess(
            strCreateProcessBinary.c_str(),
            szCommandLine.data(),
            NULL,
            NULL,
            TRUE,
            config.dwCreationFlags,
            NULL,
            NULL,
            (STARTUPINFO*)&m_si,
            &m_pi))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Could not start '{}' with command line '{}' [{}]", command, szCommandLine.data(), SystemError(hr));
        return hr;
    }

    if (m_si.lpAttributeList != nullptr && dwMajor >= 6)
    {
        const auto pk32 = ExtensionLibrary::GetLibrary<Kernel32Extension>();
        if (pk32 == nullptr)
        {
            Log::Error("Failed to obtain the Kernel32.dll as an extension library");
            return E_FAIL;
        }

        pk32->DeleteProcThreadAttributeList(m_si.lpAttributeList);
        CloseHandle(m_hParentProcess);
    }

    return S_OK;
}

HRESULT Main::LaunchWMI()
{
    HRESULT hr = E_FAIL;

    WCHAR szMyselfName[ORC_MAX_PATH];
    hr = GetProcessModuleFullPath(szMyselfName, ORC_MAX_PATH);
    if (FAILED(hr))
    {
        Log::Error("Failed to obtain own process full path [{}]", SystemError(hr));
        return hr;
    }

    Log::Debug(L"Running '{}'...", szMyselfName);

    bool bHasDeletionWork = false;

    if (m_DownloadTask)
    {
        if (std::find_if(
                begin(m_DownloadTask->Files()),
                end(m_DownloadTask->Files()),
                [](const DownloadTask::DownloadFile& file) -> bool { return file.bDelete == true; })
            != end(m_DownloadTask->Files()))
            bHasDeletionWork = true;
    }

    std::wstring strCreateProcessBinary;
    std::wstringstream cmdLineBuilder;

    if (config.bNoWait && bHasDeletionWork)
    {
        hr = ExpandFilePath(L"%ComSpec%", strCreateProcessBinary);
        if (FAILED(hr))
        {
            Log::Error("Failed to evaluate command spec %ComSpec% [{}]", SystemError(hr));
            return hr;
        }

        bool bFirst = true;
        cmdLineBuilder << L"\"" << strCreateProcessBinary << L"\" /Q /E:OFF /D /C";

        if (wcslen(szMyselfName))
        {
            cmdLineBuilder << szMyselfName;
            cmdLineBuilder << config.strCmdLineArgs;
            bFirst = false;
        }

        for (const auto& file : m_DownloadTask->Files())
        {
            if (file.bDelete)
            {
                if (bFirst)
                {
                    cmdLineBuilder << L"del \"" << file.strLocalPath << "\"";
                    bFirst = false;
                }
                else
                {
                    cmdLineBuilder << L" && del \"" << file.strLocalPath << "\"";
                    bFirst = false;
                }
            }
        }
    }
    else
    {
        cmdLineBuilder << L"\"" << szMyselfName << L"\"";
        cmdLineBuilder << config.strCmdLineArgs;
    }

    if (cmdLineBuilder.width() > MAX_CMDLINE)
    {
        Log::Error(L"Command line too long (length: {}): {}", cmdLineBuilder.width(), cmdLineBuilder.str());
        return E_FAIL;
    }

    std::vector<WCHAR> szCommandLine(MAX_CMDLINE);
    std::wstring strCommandLine = cmdLineBuilder.str();
    wcsncpy_s(szCommandLine.data(), MAX_CMDLINE, strCommandLine.c_str(), strCommandLine.size());

    WMI wmi;

    hr = wmi.Initialize();
    if (FAILED(hr))
    {
        Log::Error("Failed to initialize WMI [{}]", SystemError(hr));
        return hr;
    }

    Log::Debug(L"Starting with command line '{}'", szCommandLine.data());

    DWORD dwStatus = 0L;
    hr = wmi.WMICreateProcess(nullptr, szCommandLine.data(), config.dwCreationFlags, 0L, dwStatus);
    if (FAILED(hr) || dwStatus != 0L)
    {
        Log::Error(
            L"Could not start command line '{}' (status: {}) [{}]", szCommandLine.data(), dwStatus, SystemError(hr));
        return hr;
    }

    return S_OK;
}

HRESULT Main::LaunchSelf()
{
    HRESULT hr = E_FAIL;

    JobObject job = JobObject::GetJobObject(GetCurrentProcess());

    if (job.IsProcessInJob())
    {
        Log::Debug("Current process is running within a job");
        if (!job.IsBreakAwayAllowed())
        {
            if (config.bPreserveJob)
            {
                Log::Error("Failed to allow break away from job (changes are not allowed)");
            }
            else
            {
                hr = job.AllowBreakAway(config.bPreserveJob);
                if (FAILED(hr))
                {
                    Log::Error("Failed to allow break away from job [{}]", SystemError(hr));
                    return hr;
                }

                m_bAllowedBreakAwayOnJob = true;
                Log::Debug("Break away was not available, we enabled it");
            }
        }
    }

    WCHAR szMyselfName[ORC_MAX_PATH];
    hr = GetProcessModuleFullPath(szMyselfName, ORC_MAX_PATH);
    if (FAILED(hr))
    {
        Log::Error("Failed to obtain own process full path [{}]", SystemError(hr));
        return hr;
    }

    hr = Launch(szMyselfName, L"");

    if (m_bAllowedBreakAwayOnJob)
    {
        HRESULT _hr = job.BlockBreakAway();
        if (FAILED(_hr))
        {
            Log::Error("Failed to block break away from job [{}]", SystemError(_hr));
        }
        else
        {
            Log::Debug("Break away is now blocked again");
        }
    }

    return S_OK;
}

HRESULT Main::LaunchRun()
{
    std::wstring strToExecuteRef;
    std::wstring strToExecute;
    std::wstring strRunArgs;

    HRESULT hr = EmbeddedResource::ExtractRunWithArgs(L"", strToExecuteRef, strRunArgs);
    if (FAILED(hr))
    {
        Log::Error("Not RUN resource found to execute [{}]", SystemError(hr));
        return hr;
    }

    if (strToExecuteRef.empty())
    {
        Log::Error("RUN resource is invalid (empty)");
        return E_FAIL;
    }

    hr = EmbeddedResource::ExtractToFile(
        strToExecuteRef, L"ToExecute.exe", RESSOURCE_READ_EXECUTE_BA, config.Temporary.Path, strToExecute);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to extract resource '{}' to file [{}]", strToExecuteRef, SystemError(hr));
        return hr;
    }

    // Launch command
    hr = Launch(strToExecute, strRunArgs);
    if (FAILED(hr))
    {
        return hr;
    }

    std::shared_ptr<TerminationHandler> pTerminate;
    if (!EmbeddedResource::IsSelf(strToExecuteRef))  // Only delete the file if it was extracted
    {
        // register wait (to make sure we at least try to delete the file
        pTerminate = std::make_shared<MotherShipTerminate>(strToExecute, m_pi.hProcess, INFINITE);
        Robustness::AddTerminationHandler(pTerminate);
    }

    // waiting command completion
    DWORD dwWaitResult = WaitForSingleObject(m_pi.hProcess, INFINITE);
    if (WAIT_OBJECT_0 != dwWaitResult)
    {
        Log::Warn("Waiting for process termination did not detect process termination");
    }

    if (pTerminate)
    {
        Robustness::RemoveTerminationHandler(pTerminate);
        pTerminate = nullptr;
    }

    if (!EmbeddedResource::IsSelf(strToExecuteRef))  // Only delete the file if it was extracted
    {
        // Child has terminated
        // now delete extracted file
        bool bDeleted = false;
        DWORD dwRetries = 0;

        do
        {
            if (!DeleteFile(strToExecute.c_str()))
            {
                if (dwRetries > 20)
                {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    Log::Error(L"Could not delete file '{}' [{}]", strToExecute, SystemError(hr));
                    return hr;
                }
                else
                {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    Log::Debug(
                        L"Could not delete file '{}' (retries: {}) [{}]", strToExecute, dwRetries, SystemError(hr));
                    dwRetries++;
                    Sleep(500);
                }
            }
            else
            {
                Log::Debug(L"Successfully deleted file '{}'", strToExecute);
                bDeleted = true;
            }

        } while (!bDeleted);
    }

    Log::Debug("Done");

    CloseHandle(m_pi.hThread);
    CloseHandle(m_pi.hProcess);

    return S_OK;
}

HRESULT Main::DownloadLocalCopy()
{
    HRESULT hr = E_FAIL;

    if (!m_DownloadTask)
    {
        Log::Error("No download task configured to download local copy");
        return E_INVALIDARG;
    }

    if (FAILED(hr = m_DownloadTask->Initialize(config.bNoWait ? true : false)))
    {
        Log::Error("Failed to initialize download task [{}]", SystemError(hr));
    }
    else if (FAILED(hr = m_DownloadTask->Finalise()))
    {
        Log::Error("Failed to finalize download task [{}]", SystemError(hr));
    }
    else
    {
        return S_OK;
    }

    auto retry = m_DownloadTask->GetRetryTask();
    if (!retry)
    {
        return hr;
    }

    Log::Warn("Download failed with specified method but alternative is available");
    hr = retry->Initialize(config.bNoWait ? true : false);
    if (FAILED(hr))
    {
        Log::Error("Failed to initialize download task [{}]", SystemError(hr));
        return hr;
    }

    hr = retry->Finalise();
    if (FAILED(hr))
    {
        Log::Error("Failed to finalize download task [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    hr = ChangeTemporaryEnvironment();
    if (FAILED(hr))
    {
        Log::Warn("Failed to modify temp directory [{}]", SystemError(hr));
    }

    if (config.bUseLocalCopy && m_DownloadTask)
    {
        std::wstring strProcessBinary;

        hr = SystemDetails::GetProcessBinary(strProcessBinary);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to obtain process' binary, cancelling download task [{}]", SystemError(hr));
        }
        else
        {
            auto location = SystemDetails::GetPathLocation(strProcessBinary);

            switch (location)
            {
                case SystemDetails::DriveType::Drive_Unknown:
                case SystemDetails::DriveType::Drive_No_Root_Dir: {
                    Log::Warn(L"Running from invalid location '{}', canceling download task", strProcessBinary);
                }
                break;
                case SystemDetails::DriveType::Drive_RamDisk:
                case SystemDetails::DriveType::Drive_Removable:
                case SystemDetails::DriveType::Drive_CDRom:
                case SystemDetails::DriveType::Drive_Fixed: {
                    m_console.Print(L"Running from local device '{}', download task ignored", strProcessBinary);
                    break;
                }
                case SystemDetails::DriveType::Drive_Remote: {
                    m_console.Print(L"Running from remote path \"{}\", downloading to local device", strProcessBinary);

                    hr = DownloadLocalCopy();
                    if (FAILED(hr))
                    {
                        Log::Error(L"Downloading to local device failed, download task ignored [{}]", SystemError(hr));
                        break;
                    }

                    return S_OK;  // Download task succeded, we're done
                }
                default:
                    break;
            }
        }
    }

    if (config.bNoWait)
    {
        if (config.bUseWMI)
            return LaunchWMI();
        else
            return LaunchSelf();
    }
    else
    {
        return LaunchRun();
    }
}
