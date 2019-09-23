//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "FileCopyDownloadTask.h"

#include "ParameterCheck.h"

#include <sstream>

#include "BITSDownloadTask.h"

using namespace std;

using namespace Orc;

static DWORD WINAPI CopyProgressRoutine(
    _In_ LARGE_INTEGER TotalFileSize,
    _In_ LARGE_INTEGER TotalBytesTransferred,
    _In_ LARGE_INTEGER StreamSize,
    _In_ LARGE_INTEGER StreamBytesTransferred,
    _In_ DWORD dwStreamNumber,
    _In_ DWORD dwCallbackReason,
    _In_ HANDLE hSourceFile,
    _In_ HANDLE hDestinationFile,
    _In_opt_ LPVOID lpData)
{
    DBG_UNREFERENCED_PARAMETER(TotalFileSize);
    DBG_UNREFERENCED_PARAMETER(TotalBytesTransferred);
    DBG_UNREFERENCED_PARAMETER(StreamSize);
    DBG_UNREFERENCED_PARAMETER(StreamBytesTransferred);
    DBG_UNREFERENCED_PARAMETER(dwStreamNumber);
    DBG_UNREFERENCED_PARAMETER(dwCallbackReason);
    DBG_UNREFERENCED_PARAMETER(hSourceFile);
    DBG_UNREFERENCED_PARAMETER(hDestinationFile);
    DBG_UNREFERENCED_PARAMETER(lpData);
    return PROGRESS_CONTINUE;
}

HRESULT FileCopyDownloadTask::Initialize(const bool bDelayedDeletion)
{
    HRESULT hr = E_FAIL;
    m_bDelayedDeletion = bDelayedDeletion;
    for (const auto& file : m_files)
    {
        BOOL bCancelled = false;
        std::wstring strRemotePath = GetRemoteFullPath(file.strRemoteName);

        std::wstring strLocalPath;
        GetOutputFile(file.strLocalPath.c_str(), strLocalPath, true);

        log::Info(_L_, L"Copying %s to %s\r\n", strRemotePath.c_str(), strLocalPath.c_str());

        if (!CopyFileEx(
                strRemotePath.c_str(),
                strLocalPath.c_str(),
                CopyProgressRoutine,
                NULL,
                &bCancelled,
                COPY_FILE_ALLOW_DECRYPTED_DESTINATION))
        {
            log::Error(
                _L_,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"Failed to copy %s to %s\r\n",
                strRemotePath.c_str(),
                strLocalPath.c_str());
            return hr;
        }

        log::Verbose(_L_, L"Successfully copied %s to %s\r\n", strRemotePath.c_str(), strLocalPath.c_str());
    }
    return S_OK;
}

std::wstring FileCopyDownloadTask::GetCompletionCommandLine()
{
    HRESULT hr = E_FAIL;

    std::wstring strCmdSpec;
    if (FAILED(hr = GetInputFile(L"%ComSpec%", strCmdSpec)))
    {
        log::Error(_L_, hr, L"Failed to determine command\r\n");
    }
    else
    {
        wstringstream cmdLine;
        bool bFirst = true;
        cmdLine << L"\"" << strCmdSpec << L"\" /Q /E:OFF /D /C";

        if (!m_strCmd.empty())
        {
            cmdLine << m_strCmd;
            bFirst = false;
        }

        if (!m_bDelayedDeletion)
        {
            for (const auto& file : m_files)
            {
                if (file.bDelete)
                {
                    if (bFirst)
                    {
                        cmdLine << L"del \"" << file.strLocalPath << "\"";
                        bFirst = false;
                    }
                    else
                    {
                        cmdLine << L"&& del \"" << file.strLocalPath << "\"";
                        bFirst = false;
                    }
                }
            }
        }
        return cmdLine.str();
    }
    return std::wstring();
}

HRESULT FileCopyDownloadTask::Finalise()
{
    HRESULT hr = E_FAIL;

    std::wstring strCmdSpec;
    if (FAILED(hr = GetInputFile(L"%ComSpec%", strCmdSpec)))
    {
        log::Error(_L_, hr, L"Failed to determine command\r\n");
    }

    std::wstring strCmdLine = GetCompletionCommandLine();

    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));

    log::Info(_L_, L"Running completion command \"%s\"\r\n", strCmdLine.c_str());
    if (!CreateProcessW(
            strCmdSpec.c_str(),
            (LPWSTR)strCmdLine.c_str(),
            NULL,
            NULL,
            FALSE,
            CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB | DETACHED_PROCESS,
            NULL,
            NULL,
            &si,
            &pi))
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to create process %s with cmdLine: %s\r\n",
            strCmdSpec.c_str(),
            strCmdLine.c_str());
        return hr;
    }
    log::Verbose(_L_, L"Successfully created completion process\r\n");

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return S_OK;
}

std::wstring FileCopyDownloadTask::GetRemoteFullPath(const std::wstring& strRemoteName)
{
    wstringstream stream;
    stream << L"\\\\" << m_strServer << m_strPath << L"\\" << strRemoteName;
    return stream.str();
}

std::shared_ptr<DownloadTask> FileCopyDownloadTask::GetRetryTask()
{
    // SMB always makes it possible to attempt BITS over SMB
    auto retval = std::make_shared<BITSDownloadTask>(_L_, m_strJobName.c_str());

    retval->m_files = m_files;
    retval->m_Protocol = DownloadTask::BITSProtocol::BITS_SMB;
    retval->m_strCmd = m_strCmd;
    retval->m_strPath = m_strPath;
    retval->m_strServer = m_strServer;

    return retval;
}
