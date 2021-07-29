//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "CopyFileAgent.h"

#include "LogFileWriter.h"

#include <sstream>

using namespace std;
using namespace Orc;

#pragma comment(lib, "Mpr.lib")

HRESULT CopyFileAgent::Initialize()
{
    if (m_config.Method != OutputSpec::UploadMethod::FileCopy)
        return E_UNEXPECTED;

    if (m_config.Mode != OutputSpec::UploadMode::Synchronous)
    {
        log::Error(_L_, E_NOTIMPL, L"CopyFileAgent: unsupported upload mode (only synchronous is supported)\r\n");
        return E_NOTIMPL;  // FileCopy only supports synchronous operation
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
        WCHAR szUNC[MAX_PATH];
        ZeroMemory(szUNC, MAX_PATH * sizeof(WCHAR));

        stream << L"\\\\" << m_config.ServerName << m_config.RootPath;
        stream.str()._Copy_s(szUNC, MAX_PATH, MAX_PATH);
        nr.lpRemoteName = szUNC;

        WCHAR szUser[MAX_PATH];
        WCHAR szPass[MAX_PATH];
        ZeroMemory(szUser, MAX_PATH * sizeof(WCHAR));
        ZeroMemory(szPass, MAX_PATH * sizeof(WCHAR));
        m_config.UserName._Copy_s(szUser, MAX_PATH, MAX_PATH);
        szUser[m_config.UserName.size()] = L'\0';
        m_config.Password._Copy_s(szPass, MAX_PATH, MAX_PATH);
        szPass[m_config.UserName.size()] = L'\0';

        DWORD dwRet = 0;

        if ((dwRet = WNetAddConnection2(&nr, szPass, szUser, CONNECT_TEMPORARY)) != NO_ERROR)
        {
            log::Error(_L_, HRESULT_FROM_WIN32(dwRet), L"Failed to add a connection to %s\r\n", szUNC);
        }
        else
        {
            bAddedConnection = true;
        }
        SecureZeroMemory(szUser, MAX_PATH * sizeof(WCHAR));
        SecureZeroMemory(szPass, MAX_PATH * sizeof(WCHAR));
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
CopyFileAgent::UploadFile(const std::wstring& strLocalName, const std::wstring& strRemoteName, bool bDeleteWhenCopied)
{
    HRESULT hr = E_FAIL;

    wstring strFullPath = GetRemoteFullPath(strRemoteName);

    BOOL bCancelled = FALSE;

    if (!CopyFileEx(
            strLocalName.c_str(),
            strFullPath.c_str(),
            CopyProgressRoutine,
            NULL,
            &bCancelled,
            COPY_FILE_ALLOW_DECRYPTED_DESTINATION))
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to copy %s to %s\r\n",
            strLocalName.c_str(),
            strFullPath.c_str());
        return hr;
    }

    log::Verbose(_L_, L"Successfully copied %s to %s\r\n", strLocalName.c_str(), strFullPath.c_str());

    if (bDeleteWhenCopied)
    {
        if (!DeleteFile(strLocalName.c_str()))
        {
            if (GetLastError() != ERROR_FILE_NOT_FOUND)
            {
                log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to delete file %s after upload\r\n");
                return hr;
            }
        }
    }

    return S_OK;
}

HRESULT CopyFileAgent::IsComplete(bool bReadyToExit)
{
    DBG_UNREFERENCED_PARAMETER(bReadyToExit);
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

HRESULT CopyFileAgent::CheckFileUpload(const std::wstring& strRemoteName, PDWORD pdwFileSize)
{
    if (pdwFileSize)
        *pdwFileSize = 0L;

    wstring strFullPath = GetRemoteFullPath(strRemoteName);

    if (pdwFileSize)
    {
        *pdwFileSize = 0L;
        WIN32_FILE_ATTRIBUTE_DATA fileData;
        if (!GetFileAttributesEx(strFullPath.c_str(), GetFileExInfoStandard, &fileData))
        {
            if (GetLastError() == ERROR_FILE_NOT_FOUND)
                return S_FALSE;
            *pdwFileSize = MAXDWORD;
        }
        else
        {
            if (fileData.nFileSizeHigh > 0)
            {
                *pdwFileSize = MAXDWORD;
            }
            else
            {
                *pdwFileSize = fileData.nFileSizeLow;
            }
        }
    }
    else
    {
        if (INVALID_FILE_ATTRIBUTES == GetFileAttributes(strFullPath.c_str()))
        {
            if (GetLastError() == ERROR_FILE_NOT_FOUND)
                return S_FALSE;
        }
    }

    return S_OK;
}

std::wstring CopyFileAgent::GetRemoteFullPath(const std::wstring& strRemoteName)
{
    wstringstream stream;

    stream << L"\\\\" << m_config.ServerName << m_config.RootPath << L"\\" << strRemoteName;

    return stream.str();
}

std::wstring CopyFileAgent::GetRemotePath(const std::wstring& strRemoteName)
{
    wstringstream stream;

    stream << L"\\\\" << m_config.ServerName << m_config.RootPath << L"\\" << strRemoteName;

    return stream.str();
}

CopyFileAgent::~CopyFileAgent() {}
