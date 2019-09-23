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
#include "LogFileWriter.h"

#include "JobObject.h"

#include "DownloadTask.h"

#include <sstream>

using namespace std;

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
                        L"ERROR: Could not delete file \"%s\" (hr=0x%lx)\r\n",
                        m_FileToDelete.c_str(),
                        HRESULT_FROM_WIN32(GetLastError()));
                    return HRESULT_FROM_WIN32(GetLastError());
                }
                else
                {
                    wprintf_s(
                        L"VERBOSE: Could not delete file \"%s\" (hr=0x%lx,retries=%d)\r\n",
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

HRESULT Main::ChangeTemporaryEnvironment()
{
    HRESULT hr = E_FAIL;

    if (config.Temporary.Path.empty())
        return S_OK;

    if (!SetEnvironmentVariableW(L"TEMP", config.Temporary.Path.c_str()))
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to set %TEMP% to %s\r\n",
            config.Temporary.Path.c_str());
        return E_INVALIDARG;
    }
    if (!SetEnvironmentVariableW(L"TMP", config.Temporary.Path.c_str()))
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to set %TMP% to %s\r\n",
            config.Temporary.Path.c_str());
        return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT Main::Launch(const std::wstring& strToExecute, const std::wstring& strRunArgs)
{
    HRESULT hr = E_FAIL;

    log::Verbose(_L_, L"\r\nRunning %s...\r\n", strToExecute.c_str());

    wstringstream cmdLineBuilder;

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
        if (FAILED(hr = GetInputFile(L"%ComSpec%", strCreateProcessBinary)))
        {
            log::Error(_L_, hr, L"Failed to evaluate command spec (%ComSpec%)\r\n");
            return hr;
        }

        bool bFirst = true;
        cmdLineBuilder << L"\"" << strCreateProcessBinary << L"\" /Q /E:OFF /D /C";

        if (!strToExecute.empty())
        {
            cmdLineBuilder << strToExecute;
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
        strCreateProcessBinary = strToExecute;
        cmdLineBuilder << L"\"" << strToExecute << L"\" " << strRunArgs;
        cmdLineBuilder << config.strCmdLineArgs;
    }

    if (cmdLineBuilder.width() > MAX_CMDLINE)
    {
        log::Error(
            _L_,
            E_INVALIDARG,
            L"Command line too long (length=%d): \t%s\r\n",
            cmdLineBuilder.width(),
            cmdLineBuilder.str());
        return E_INVALIDARG;
    }

    std::vector<WCHAR> szCommandLine(MAX_CMDLINE);
    std::wstring strCommandLine = cmdLineBuilder.str();
    wcsncpy_s(szCommandLine.data(), MAX_CMDLINE, strCommandLine.c_str(), strCommandLine.size());

    DWORD dwMajor = 0, dwMinor = 0;
    SystemDetails::GetOSVersion(dwMajor, dwMinor);

    HANDLE hParent = INVALID_HANDLE_VALUE;

    if (dwMajor >= 6 && config.dwParentID != 0)
    {
        const auto pk32 = ExtensionLibrary::GetLibrary<Kernel32Extension>(_L_);
        if (pk32 == nullptr)
        {
            log::Error(_L_, E_FAIL, L"Failed to obtain the Kernel32.dll as an extension library\r\n");
            return E_FAIL;
        }

        SIZE_T sizeToAlloc = 0;

        if (FAILED(hr = pk32->InitializeProcThreadAttributeList(nullptr, 1, 0, &sizeToAlloc))
            && hr != HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))
        {
            log::Error(_L_, hr, L"Failed to obtain attrlist size\r\n");
            return hr;
        }

        PPROC_THREAD_ATTRIBUTE_LIST pAttrList =
            (PPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeToAlloc);
        if (pAttrList == nullptr)
            return E_OUTOFMEMORY;

        if (FAILED(hr = pk32->InitializeProcThreadAttributeList(pAttrList, 1, 0, &sizeToAlloc)))
        {
            log::Error(_L_, hr, L"Failed to obtain attrlist size\r\n");
            return hr;
        }

        if ((m_hParentProcess = OpenProcess(PROCESS_CREATE_PROCESS, FALSE, config.dwParentID)) == NULL)
        {
            log::Error(
                _L_,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"Failed to open parent process (PID=%d) with PROCESS_CREATE_PROCESS\r\n",
                config.dwParentID);
            return hr;
        }

        if (FAILED(
                hr = pk32->UpdateProcThreadAttribute(
                    pAttrList,
                    0,
                    PROC_THREAD_ATTRIBUTE_PARENT_PROCESS,
                    &m_hParentProcess,
                    sizeof(hParent),
                    nullptr,
                    nullptr)))
        {
            log::Error(_L_, hr, L"Failed to add process handle to attribute list\r\n");
            return hr;
        }

        m_si.StartupInfo.cb = sizeof(STARTUPINFOEX);
        m_si.lpAttributeList = pAttrList;
        config.dwCreationFlags |= EXTENDED_STARTUPINFO_PRESENT;
    }

    config.dwCreationFlags |= CREATE_UNICODE_ENVIRONMENT;

    log::Verbose(_L_, L"Starting \"%s\" with command line \"%s\"", strToExecute.c_str(), szCommandLine);

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
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"Could not start \"%s\" with command line \"%s\"\r\n",
            strToExecute.c_str(),
            szCommandLine);
        return hr;
    }

    if (m_si.lpAttributeList != nullptr && dwMajor >= 6)
    {
        const auto pk32 = ExtensionLibrary::GetLibrary<Kernel32Extension>(_L_);
        if (pk32 == nullptr)
        {
            log::Error(_L_, E_FAIL, L"Failed to obtain the Kernel32.dll as an extension library\r\n");
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

    WCHAR szMyselfName[MAX_PATH];
    if (FAILED(hr = GetProcessModuleFullPath(szMyselfName, MAX_PATH)))
    {
        log::Error(_L_, hr, L"Failed to obtain own process full path\r\n");
        return hr;
    }

    log::Verbose(_L_, L"\r\nRunning %s...\r\n", szMyselfName);

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

    wstringstream cmdLineBuilder;

    if (config.bNoWait && bHasDeletionWork)
    {
        if (FAILED(hr = GetInputFile(L"%ComSpec%", strCreateProcessBinary)))
        {
            log::Error(_L_, hr, L"Failed to evaluate command spec (%ComSpec%)\r\n");
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
        log::Error(
            _L_,
            hr = E_INVALIDARG,
            L"Command line too long (length=%d): \t%s\r\n",
            cmdLineBuilder.width(),
            cmdLineBuilder.str());
        return hr;
    }

    std::vector<WCHAR> szCommandLine(MAX_CMDLINE);
    std::wstring strCommandLine = cmdLineBuilder.str();
    wcsncpy_s(szCommandLine.data(), MAX_CMDLINE, strCommandLine.c_str(), strCommandLine.size());

    WMIUtil wmi(_L_);

    if (FAILED(hr = wmi.Initialize()))
    {
        log::Error(_L_, hr, L"Failed to initialize WMI\r\n");
        return hr;
    }

    log::Verbose(_L_, L"Starting with command line \"%s\"", szCommandLine);

    DWORD dwStatus = 0L;
    if (FAILED(hr = wmi.WMICreateProcess(nullptr, szCommandLine.data(), config.dwCreationFlags, 0L, dwStatus))
        || dwStatus != 0L)
    {
        log::Error(_L_, hr, L"Could not start command line \"%s\" (status=%d)\r\n", szCommandLine, dwStatus);
        return hr;
    }

    return S_OK;
}

HRESULT Main::LaunchSelf()
{
    HRESULT hr = E_FAIL;

    JobObject job = JobObject::GetJobObject(_L_, GetCurrentProcess());

    if (job.IsProcessInJob())
    {
        log::Verbose(_L_, L"Current process is running within a job!\r\n");
        if (!job.IsBreakAwayAllowed())
        {
            if (config.bPreserveJob)
            {
                log::Error(_L_, hr, L"Failed to allow break away from job (changes are not allowed)\r\n");
            }
            else
            {
                if (FAILED(hr = job.AllowBreakAway(config.bPreserveJob)))
                {
                    log::Error(_L_, hr, L"Failed to allow break away from job\r\n");
                    return hr;
                }
                m_bAllowedBreakAwayOnJob = true;
                log::Verbose(_L_, L"Break away was not available, we enabled it!\r\n");
            }
        }
    }

    WCHAR szMyselfName[MAX_PATH];
    if (FAILED(hr = GetProcessModuleFullPath(szMyselfName, MAX_PATH)))
    {
        log::Error(_L_, hr, L"Failed to obtain own process full path\r\n");
        return hr;
    }
    hr = Launch(szMyselfName, L"");

    if (m_bAllowedBreakAwayOnJob)
    {
        HRESULT _hr = E_FAIL;
        if (FAILED(_hr = job.BlockBreakAway()))
        {
            log::Error(_L_, hr, L"Failed to block break away from job\r\n");
        }
        else
        {
            log::Verbose(_L_, L"Break away is now blocked again\r\n");
        }
    }
    return S_OK;
}

HRESULT Main::LaunchRun()
{
    HRESULT hr = E_FAIL;
    // Do my stuff here...
    wstring strToExecuteRef;
    wstring strToExecute;
    wstring strRunArgs;

    if (FAILED(hr = EmbeddedResource::ExtractRunWithArgs(_L_, L"", strToExecuteRef, strRunArgs)))
    {
        log::Error(_L_, hr, L"Not RUN ressource found to execute\r\n");
        return hr;
    }

    if (strToExecuteRef.empty())
    {
        log::Error(_L_, E_FAIL, L"RUN ressource is invalid (empty)\r\n");
        return E_FAIL;
    }

    if (FAILED(
            hr = EmbeddedResource::ExtractToFile(
                _L_,
                strToExecuteRef,
                L"ToExecute.exe",
                RESSOURCE_READ_EXECUTE_BA,
                config.Temporary.Path,
                strToExecute)))
    {
        log::Error(_L_, hr, L"Failed to extract resource %s to file\r\n", strToExecuteRef.c_str());
        return hr;
    }

    // Launch command
    if (FAILED(hr = Launch(strToExecute, strRunArgs)))
        return hr;

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
        log::Warning(_L_, E_UNEXPECTED, L"Waiting for process termination did not detect process termination\r\n");
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
                    log::Error(
                        _L_,
                        hr = HRESULT_FROM_WIN32(GetLastError()),
                        L"Could not delete file \"%s\"\r\n",
                        strToExecute.c_str());
                    return hr;
                }
                else
                {
                    log::Verbose(
                        _L_,
                        L"VERBOSE: Could not delete file \"%s\" (hr=0x%lx,retries=%d)\r\n",
                        strToExecute.c_str(),
                        HRESULT_FROM_WIN32(GetLastError()),
                        dwRetries);
                    dwRetries++;
                    Sleep(500);
                }
            }
            else
            {
                log::Verbose(_L_, L"VERBOSE: Successfully deleted file \"%s\"\r\n", strToExecute.c_str());
                bDeleted = true;
            }

        } while (!bDeleted);
    }

    log::Verbose(_L_, L"\r\nDone.\r\n");

    CloseHandle(m_pi.hThread);
    CloseHandle(m_pi.hProcess);

    return S_OK;
}

HRESULT Main::DownloadLocalCopy()
{
    HRESULT hr = E_FAIL;

    if (!m_DownloadTask)
    {
        log::Error(_L_, E_INVALIDARG, L"No download task configured to download local copy\r\n");
        return E_INVALIDARG;
    }

    if (FAILED(hr = m_DownloadTask->Initialize(config.bNoWait ? true : false)))
    {
        log::Error(_L_, hr, L"Failed to initialize download task\r\n");
    }
    else if (FAILED(hr = m_DownloadTask->Finalise()))
    {
        log::Error(_L_, hr, L"Failed to finalize download task\r\n");
    }
    else
    {
        return S_OK;
    }

    auto retry = m_DownloadTask->GetRetryTask();
    if (!retry)
        return hr;

    log::Info(_L_, L"Download failed with specified method but alternative is available\r\n");
    if (FAILED(hr = retry->Initialize(config.bNoWait ? true : false)))
    {
        log::Error(_L_, hr, L"Failed to initialize download task\r\n");
        return hr;
    }

    if (FAILED(hr = retry->Finalise()))
    {
        log::Error(_L_, hr, L"Failed to finalize download task\r\n");
        return hr;
    }

    return S_OK;
}

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = ChangeTemporaryEnvironment()))
    {
        log::Warning(_L_, hr, L"Failed to modify temp directory\r\n");
    }

    if (config.bUseLocalCopy && m_DownloadTask)
    {
        std::wstring strProcessBinary;

        if (FAILED(hr = SystemDetails::GetProcessBinary(strProcessBinary)))
        {
            log::Error(_L_, hr, L"Failed to obtain process' binary, cancelling download task\r\n");
        }
        else
        {
            auto location = SystemDetails::GetPathLocation(strProcessBinary);

            switch (location)
            {
                case SystemDetails::DriveType::Drive_Unknown:
                case SystemDetails::DriveType::Drive_No_Root_Dir:
                {
                    log::Warning(
                        _L_,
                        HRESULT_FROM_WIN32(ERROR_INVALID_DRIVE),
                        L"Running from invalid location \"%s\", canceling download task\r\n",
                        strProcessBinary.c_str());
                }
                break;
                case SystemDetails::DriveType::Drive_RamDisk:
                case SystemDetails::DriveType::Drive_Removable:
                case SystemDetails::DriveType::Drive_CDRom:
                case SystemDetails::DriveType::Drive_Fixed:
                {
                    log::Info(
                        _L_, L"Running from local device \"%s\", download task ignored\r\n", strProcessBinary.c_str());
                    break;
                }
                case SystemDetails::DriveType::Drive_Remote:
                {
                    log::Info(
                        _L_,
                        L"Running from remote path \"%s\", downloading to local device\r\n",
                        strProcessBinary.c_str());
                    if (FAILED(hr = DownloadLocalCopy()))
                    {
                        log::Error(
                            _L_,
                            hr,
                            L"Downloading to local device failed, download task ignored\r\n",
                            strProcessBinary.c_str());
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