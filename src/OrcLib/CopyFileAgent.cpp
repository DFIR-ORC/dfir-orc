//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "CopyFileAgent.h"

#include <sstream>

using namespace std;
using namespace Orc;

#pragma comment(lib, "Mpr.lib")

HRESULT CopyFileAgent::Initialize()
{
    if (m_config.Method != OutputSpec::UploadMethod::FileCopy)
    {
        Log::Error("CopyFileAgent failed to initialize because of invalid method");
        return E_UNEXPECTED;
    }

    if (m_config.Mode != OutputSpec::UploadMode::Synchronous)
    {
        Log::Error("CopyFileAgent failed to initialize because of unsupported upload mode");
        return E_UNEXPECTED;  // FileCopy only supports synchronous operation
    }

    if (!m_config.UserName.empty())
    {
        NETRESOURCE nr;

        nr.dwScope = 0L;
        nr.dwType = RESOURCETYPE_DISK;
        nr.dwDisplayType = 0L;
        nr.dwUsage = 0L;

        nr.lpLocalName = NULL;

        nr.lpComment = NULL;
        nr.lpProvider = NULL;

        wstringstream stream;
        WCHAR szUNC[ORC_MAX_PATH];
        ZeroMemory(szUNC, ORC_MAX_PATH * sizeof(WCHAR));

        stream << L"\\\\" << m_config.ServerName << m_config.RootPath;
        stream.str()._Copy_s(szUNC, ORC_MAX_PATH, ORC_MAX_PATH);
        nr.lpRemoteName = szUNC;

        WCHAR szUser[ORC_MAX_PATH];
        WCHAR szPass[ORC_MAX_PATH];
        ZeroMemory(szUser, ORC_MAX_PATH * sizeof(WCHAR));
        ZeroMemory(szPass, ORC_MAX_PATH * sizeof(WCHAR));
        m_config.UserName._Copy_s(szUser, ORC_MAX_PATH, ORC_MAX_PATH);
        szUser[m_config.UserName.size()] = L'\0';
        m_config.Password._Copy_s(szPass, ORC_MAX_PATH, ORC_MAX_PATH);
        szPass[m_config.Password.size()] = L'\0';

        DWORD dwRet = 0;

        if ((dwRet = WNetAddConnection2(&nr, szPass, szUser, CONNECT_TEMPORARY)) != NO_ERROR)
        {
            Log::Error(L"CopyFileAgent failed to add a connection to '{}' [{}]", szUNC, Win32Error(dwRet));
        }
        else
        {
            bAddedConnection = true;
        }
        SecureZeroMemory(szUser, ORC_MAX_PATH * sizeof(WCHAR));
        SecureZeroMemory(szPass, ORC_MAX_PATH * sizeof(WCHAR));
    }

    return S_OK;
}

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

HRESULT
CopyFileAgent::UploadFile(
    const std::wstring& source,
    const std::wstring& strRemoteName,
    bool bDeleteWhenCopied,
    const std::shared_ptr<const UploadMessage>& request)
{
    HRESULT hr = E_FAIL;

    wstring strFullPath = GetRemoteFullPath(strRemoteName);

    BOOL bCancelled = FALSE;

    if (!CopyFileEx(
            source.c_str(),
            strFullPath.c_str(),
            CopyProgressRoutine,
            NULL,
            &bCancelled,
            COPY_FILE_ALLOW_DECRYPTED_DESTINATION))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed to copy '{}' to '{}' [{}]", source, strFullPath, SystemError(hr));
        return hr;
    }

    Log::Debug(L"Successfully copied '{}' to '{}'", source, strFullPath);

    if (bDeleteWhenCopied)
    {
        if (!DeleteFile(source.c_str()))
        {
            if (GetLastError() != ERROR_FILE_NOT_FOUND)
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                Log::Error(L"Failed to delete file '{}' after upload [{}]", source, SystemError(hr));
                return hr;
            }
        }
    }

    return S_OK;
}

HRESULT CopyFileAgent::IsComplete(bool bReadyToExit, bool& hasFailure)
{
    DBG_UNREFERENCED_PARAMETER(bReadyToExit);
    hasFailure = false;
    return S_OK;
}

HRESULT CopyFileAgent::Cancel()
{
    return S_OK;
}

HRESULT CopyFileAgent::UnInitialize()
{
    if (bAddedConnection)
    {
        wstringstream stream;

        stream << L"\\\\" << m_config.ServerName << m_config.RootPath << L"\\";

        WNetCancelConnection2(stream.str().c_str(), 0L, TRUE);
    }
    return S_OK;
}

HRESULT CopyFileAgent::CheckFileUpload(const std::wstring& strRemoteName, std::optional<DWORD>& fileSize)
{
    std::wstring strFullPath = GetRemoteFullPath(strRemoteName);

    WIN32_FILE_ATTRIBUTE_DATA fileData;
    if (!GetFileAttributesEx(strFullPath.c_str(), GetFileExInfoStandard, &fileData))
    {
        DWORD lastError = GetLastError();
        Log::Error("Failed GetFileAttributesEx in CheckFileUpload [{}]", SystemError(lastError));
        if (lastError == ERROR_FILE_NOT_FOUND)
        {
            return S_FALSE;
        }
    }
    else
    {
        if (fileData.nFileSizeHigh > 0)
        {
            fileSize = MAXDWORD;
        }
        else
        {
            fileSize = fileData.nFileSizeLow;
        }
    }

    return S_OK;
}

std::wstring CopyFileAgent::GetRemoteFullPath(const std::wstring& strRemoteName)
{
    wstringstream stream;

    stream << L"\\\\" << m_config.ServerName << m_config.RootPath;
    if (m_config.RootPath.empty()
        || (m_config.RootPath[m_config.RootPath.size() - 1] != L'\\'
            && m_config.RootPath[m_config.RootPath.size() - 1] != L'/'))
    {
        stream << L"\\";
    }

    stream << strRemoteName;

    return stream.str();
}

std::wstring CopyFileAgent::GetRemotePath(const std::wstring& strRemoteName)
{
    wstringstream stream;

    stream << L"\\\\" << m_config.ServerName << m_config.RootPath << L"\\" << strRemoteName;

    return stream.str();
}

CopyFileAgent::~CopyFileAgent() {}
