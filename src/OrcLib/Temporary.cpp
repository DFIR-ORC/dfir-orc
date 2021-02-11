//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "Temporary.h"

#include "ParameterCheck.h"
#include "SecurityDescriptor.h"
#include "Text/Fmt/error_code.h"

#include <boost/scope_exit.hpp>

#include <string>
#include <sstream>

#include "Log/Log.h"

using namespace std;

using namespace Orc;

static const auto CREATION_RETRIES = 100;

static const auto WAIT_CREATE_TIME = 100;

/*
    UtilPathIsDirectory

    Whether a path is a directory

    Parameters:
        pwszPath    -   The path



    Return:
        Whether the path is a directory
*/
BOOL Orc::UtilPathIsDirectory(__in PCWSTR pwszPath)
{
    DWORD dwAttributes = GetFileAttributes(pwszPath);

    if (dwAttributes == INVALID_FILE_ATTRIBUTES)
        return FALSE;

    return (dwAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

/*
    UtilGetTempDirPath

    Gets a temp dir path

    Parameters:
        pwszTempDir -   This will contain the temp dir path
        cchPathSize  -   This will contain the size of the buffer
*/
HRESULT Orc::UtilGetTempDirPath(__out_ecount(cchPathSize) PWSTR pwszTempDir, __in DWORD cchPathSize)
{
    WCHAR wszTempDir[MAX_PATH];
    HRESULT hr = E_FAIL;

    wszTempDir[0] = 0;

    if (pwszTempDir == NULL || cchPathSize == 0)
    {
        return E_INVALIDARG;
    }

    if (!GetTempPath(cchPathSize, pwszTempDir) || pwszTempDir[0] == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (!UtilPathIsDirectory(pwszTempDir))
    {
        if (!CreateDirectory(pwszTempDir, NULL))
        {
            *pwszTempDir = NULL;
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }

    if (!GetLongPathName(pwszTempDir, wszTempDir, ARRAYSIZE(wszTempDir)))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (FAILED(hr = StringCchCopy(pwszTempDir, cchPathSize, wszTempDir)))
    {
        return hr;
    }

    return S_OK;
}

/*
    UtilGetTempFile

    Opens a handle to a temporary file in the specified dir w/ the specified
    extension, returing TRUE if success and updating the handle and providing
    the temproary file name.  wzFileName should be a pointer to a buffer of
    at least MAX_PATH.

    Parameters:
        phFile              -   This will store the file handle
        pwszDir             -   The directory where we will create the file.
                                    If this is NULL we will use the temp directory
        pwszExt             -   The extension. This is optional
        pwszFileName        -   This will hold the path of the temp file
        cchFileName         -   The buffer size in TCHARs of pwszFileName
        psa                 -   Security attributes of this file. Please see CreateFile in MSDN
        dwShareMode         -   Share mode
        dwFlags             -   Optional flags

    Return:
        NULL    -   If there was an error
        Otherwise the instance handle
*/
HRESULT Orc::UtilGetTempFile(
    __out_opt HANDLE* phFile,
    __in_opt PCWSTR pwszDir,
    __in PCWSTR pwszExt,
    __out_ecount(cchFileName) PWSTR pwszFilePath,
    __in DWORD cchFileName,
    __in_opt LPSECURITY_ATTRIBUTES psa,
    __in_opt DWORD dwShareMode,
    __in_opt DWORD dwFlags)
{
    DWORD dwRetries = 0;
    WCHAR wszDirPath[MAX_PATH];
    WCHAR wszTempFilePath[MAX_PATH];
    HRESULT hr = E_FAIL;
    HANDLE hFile = INVALID_HANDLE_VALUE;

    _ASSERT(pwszFilePath != NULL);
    _ASSERT(pwszExt != NULL);
    _ASSERT(cchFileName >= MAX_PATH);

    if (pwszFilePath == NULL || cchFileName < MAX_PATH)
        return E_INVALIDARG;

    wszTempFilePath[0] = wszDirPath[0] = 0;

    if (pwszDir == NULL)
    {
        if (FAILED(hr = UtilGetTempDirPath(wszDirPath, ARRAYSIZE(wszDirPath))))
            goto End;

        pwszDir = wszDirPath;
    }

    while (dwRetries < CREATION_RETRIES)
    {
        if (GetTempFileName(pwszDir, L"NTRACK", 0, wszTempFilePath))
        {
            if (pwszExt)
            {
                // Delete the temp file earlier created
                static_cast<void>(DeleteFile(wszTempFilePath));

                if (FAILED(hr = StringCchCat(wszTempFilePath, ARRAYSIZE(wszTempFilePath), pwszExt)))
                {
                    // LogError("StringCchCat failed with : 0x%x", hr);
                    return hr;
                }
            }

            hFile = CreateFile(
                wszTempFilePath,
                GENERIC_WRITE | GENERIC_READ,
                dwShareMode,
                psa,
                CREATE_ALWAYS,
                dwFlags | FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                NULL);

            if (hFile != INVALID_HANDLE_VALUE)
            {
                break;
            }
        }

        dwRetries++;

        Sleep(WAIT_CREATE_TIME);
    }

    if (hFile == INVALID_HANDLE_VALUE)
    {
        hr = E_FAIL;
        goto End;
    }

    // Return the long path name
    if (!GetLongPathName(wszTempFilePath, pwszFilePath, cchFileName))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto End;
    }

    if (phFile)
    {
        *phFile = hFile;
    }
    else
    {
        if (hFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
        }
    }

    hr = S_OK;

End:

    if (FAILED(hr))
    {
        if (hFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
        }
    }

    return hr;
}

HRESULT Orc::UtilGetTempFile(
    __out_opt HANDLE* phFile,
    __in_opt PCWSTR pwszDir,
    __in PCWSTR pwszExt,
    std::wstring& strFilePath,
    __in_opt LPSECURITY_ATTRIBUTES psa,
    __in_opt DWORD dwShareMode,
    /* FILE_SHARE_READ */ __in_opt DWORD dwFlags)
{
    WCHAR szTempFilePath[MAX_PATH];
    ZeroMemory(szTempFilePath, MAX_PATH * sizeof(WCHAR));
    HRESULT hr = UtilGetTempFile(phFile, pwszDir, pwszExt, szTempFilePath, MAX_PATH, psa, dwShareMode, dwFlags);
    if (FAILED(hr))
        return hr;
    strFilePath.assign(szTempFilePath);
    return S_OK;
}

/*
    UtilExpandEnvVariable

    Expands the environment variables in a string



    Parameters:
        pwszData    -   The string that we need to expand
        pstrData    -   This will contain the expanded string
*/
HRESULT UtilExpandEnvVariable(__in PCWSTR pwszData, __out std::wstring& strData)
{
    auto hr = E_FAIL;

    if (pwszData == NULL)
        return E_INVALIDARG;

    DWORD dw = ExpandEnvironmentStrings(pwszData, NULL, 0);
    PWSTR pszNewValue = new WCHAR[dw];
    if (pszNewValue == NULL)
        return E_OUTOFMEMORY;

    BOOST_SCOPE_EXIT(&pszNewValue) { delete[] pszNewValue; }
    BOOST_SCOPE_EXIT_END;

    dw = ExpandEnvironmentStrings(pwszData, pszNewValue, dw);
    if (dw == 0)
        return HRESULT_FROM_WIN32(GetLastError());

    strData.assign(pszNewValue);
    return S_OK;
}

/*
    UtilFileExists

    Checks whether a file exists or not

    Parameters:
        pwszFilePath    -   The file path



    Return:
        TRUE    -   Success, file exists
        FALSE   -   otherwise



    Notes:
        This will not work for a directory
*/
BOOL UtilFileExists(__in PCWSTR pwszFilePath)
{
    DWORD dwFileAttributes = 0;
    HANDLE hFile = NULL;
    wstring strFilePath;
    HRESULT hr = E_FAIL;

    if (FAILED(hr = UtilExpandEnvVariable(pwszFilePath, strFilePath)))
        return FALSE;

    dwFileAttributes = GetFileAttributes(strFilePath.c_str());

    if (!dwFileAttributes && GetLastError() == ERROR_FILE_NOT_FOUND)
        return FALSE;

    if (dwFileAttributes == FILE_ATTRIBUTE_DIRECTORY || dwFileAttributes == INVALID_FILE_ATTRIBUTES)
        return FALSE;

    hFile = CreateFile(
        strFilePath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;
    else
    {
        CloseHandle(hFile);
        hFile = NULL;
    }

    return TRUE;
}

/*
    UtilGetUniquePath



    Checks if a filename already exists in a directory. If the name does
    exist, this routine tries to come up with an alternate name for the
    file by appending a suffix to to it.



    Parameters:
        pwszDir         -   The directory path
        pwszDesiredName -   The desired name of the file. if the file exists, this
                            name will be appened with a suffix to come up with a
                            unique filename in the directory



        pstrDestPath    -   This string will receive the full path to the unique filename



    Return:
        S_OK    -   Success
        Error HR otherwise
*/
HRESULT Orc::UtilGetUniquePath(__in PCWSTR pwszDir, __in PCWSTR pwszDesiredName, __out wstring& pstrDestPath)
{
    DWORD dwLoop = 0;
    HRESULT hr = E_FAIL;

    if (pwszDir == NULL || pwszDesiredName == NULL)
    {
        return E_INVALIDARG;
    }

    wstring dir(pwszDir);

    if (dir.back() == L'\\' && dir.size() > 3)
    {
        dir.resize(dir.size() - 1);
    }

    if (FAILED(hr = VerifyDirectoryExists(dir.c_str())))
    {
        return HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    }

    wstringstream stream;
    stream << dir << L"\\" << pwszDesiredName;

    pstrDestPath = std::move(stream.str());

    for (dwLoop = 0; dwLoop < CREATION_RETRIES; dwLoop++)
    {
        if (!UtilFileExists(pstrDestPath.c_str()))
            break;

        wstringstream stream2;
        stream2 << dir << L"\\" << dwLoop + 1 << L"_" << pwszDesiredName;

        pstrDestPath = std::move(stream2.str());
    }

    if (dwLoop == CREATION_RETRIES)
    {
        return HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
    }

    return S_OK;
}

/*

UtilGetUniquePath

Checks if a filename already exists in a directory. If the name does
exist, this routine tries to come up with an alternate name for the
file by appending a suffix to to it.

Parameters:
    pwszDir         -   The directory path
    pwszDesiredName -   The desired name of the file. if the file exists, this
        name will be appened with a suffix to come up with a unique filename in
        the directory

pstrDestPath    -   This string will receive the full path to the unique filename
*/
HRESULT Orc::UtilGetUniquePath(
    __in PCWSTR pwszDir,
    __in PCWSTR pwszDesiredName,
    __out wstring& pstrDestPath,
    __out HANDLE& hFile,
    __in DWORD dwFlags,
    LPCWSTR szSDDL)
{
    DWORD dwLoop = 0;
    HRESULT hr = E_FAIL;

    if (pwszDir == NULL || pwszDesiredName == NULL)
    {
        return E_INVALIDARG;
    }

    wstring dir(pwszDir);

    while (dir.back() == L'\\' && dir.size() > 1)
    {
        dir.pop_back();
    }

    if (FAILED(hr = VerifyDirectoryExists(dir.c_str())))
    {
        return HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    }

    wstringstream stream;
    stream << dir << L"\\" << pwszDesiredName;

    pstrDestPath = std::move(stream.str());

    SecurityDescriptor sd;

    SECURITY_ATTRIBUTES sa;

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = FALSE;

    if (szSDDL != nullptr)
    {
        if (FAILED(hr = sd.ConvertFromSDDL(szSDDL)))
            return hr;

        sa.lpSecurityDescriptor = sd.GetSecurityDescriptor();
    }

    for (dwLoop = 0; dwLoop < CREATION_RETRIES; dwLoop++)
    {
        if (UtilFileExists(pstrDestPath.c_str()))
        {
            wstringstream stream;
            stream << dir << L"\\" << dwLoop + 1 << L"_" << pwszDesiredName;

            pstrDestPath = std::move(stream.str());
            continue;
        }

        hFile = CreateFile(
            pstrDestPath.c_str(),
            GENERIC_WRITE | GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            &sa,
            CREATE_NEW,
            dwFlags,
            NULL);

        if (hFile != INVALID_HANDLE_VALUE)
        {
            break;
        }
    }

    sd.FreeSecurityDescritor();

    if (dwLoop == CREATION_RETRIES)
    {
        hFile = INVALID_HANDLE_VALUE;
        pstrDestPath.clear();
        return HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
    }

    return S_OK;
}

/*
    UtilGetPath



    Get a path from directory and file name.



    Parameters:
        pwszDir         -   The directory path
        pwszDesiredName -   The desired name of the file.



        pstrDestPath    -   This string will receive the full path to the filename
*/
HRESULT Orc::UtilGetPath(__in PCWSTR pwszDir, __in PCWSTR pwszDesiredName, __out wstring& pstrDestPath)
{
    HRESULT hr = E_FAIL;

    if (pwszDir == NULL || pwszDesiredName == NULL)
    {
        return E_INVALIDARG;
    }

    wstring dir(pwszDir);

    if (dir.back() == L'\\' && dir.size() > 3)
    {
        dir.resize(dir.size() - 1);
    }

    if (FAILED(hr = VerifyDirectoryExists(dir.c_str())))
    {
        return HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    }

    wstringstream stream;
    stream << dir << L"\\" << pwszDesiredName;
    pstrDestPath = std::move(stream.str());

    return S_OK;
}

HRESULT ORCLIB_API Orc::UtilDeleteTemporaryFile(LPCWSTR pszPath)
{
    HRESULT hr = E_FAIL;
    bool bDeleted = false;
    DWORD dwRetries = 0;

    do
    {
        if (!DeleteFile(pszPath))
        {
            if (GetLastError() == ERROR_FILE_NOT_FOUND)
            {
                Log::Debug(L"File '{}' already deleted", pszPath);
                return S_OK;
            }
            if (dwRetries > DELETION_RETRIES)
            {
                Log::Debug(L"Could not delete file '{}', Delay deletion till next reboot", pszPath);
                if (!MoveFileEx(pszPath, NULL, MOVEFILE_DELAY_UNTIL_REBOOT))
                {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    Log::Error(L"Failed to delay deletion until reboot for file '{}' [{}]", pszPath, SystemError(hr));
                    return hr;
                }
                return S_OK;
            }
            else
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                Log::Debug(L"Could not delete file '{}' ([{}], retries: {})", pszPath, SystemError(hr), dwRetries);
                dwRetries++;
                Sleep(200);
            }
        }
        else
        {
            Log::Debug(L"Successfully deleted file '{}'", pszPath);
            bDeleted = true;
        }

    } while (!bDeleted);

    Log::Debug("UtilDeleteTemporaryFile: done");

    return S_OK;
}

HRESULT ORCLIB_API Orc::UtilDeleteTemporaryDirectory(const std::filesystem::path& path)
{
    for (auto& p : std::filesystem::recursive_directory_iterator(path))
    {
        if (auto hr = UtilDeleteTemporaryFile(p.path()); FAILED(hr))
            Log::Error(L"Failed to delete temp file {} [{}]", p.path(), SystemError(hr));
    }

    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec)
    {
        Log::Error(L"Failed to delete temp dir {} [{}]", path, ec);
        return HRESULT_FROM_WIN32(ec.value());
    }
    return S_OK;
}

HRESULT ORCLIB_API Orc::UtilDeleteTemporaryFile(const std::filesystem::path& path)
{
    return UtilDeleteTemporaryFile(path.c_str());
}

HRESULT ORCLIB_API Orc::UtilDeleteTemporaryFile(__in LPCWSTR pszPath, DWORD dwMaxRetries)
{
    HRESULT hr = E_FAIL;
    bool bDeleted = false;
    DWORD dwRetries = 0;

    do
    {
        if (!DeleteFile(pszPath))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());

            if (GetLastError() == ERROR_FILE_NOT_FOUND)
            {
                return S_OK;
            }
            if (dwRetries > dwMaxRetries)
            {
                if (!MoveFileEx(pszPath, NULL, MOVEFILE_DELAY_UNTIL_REBOOT))
                {
                    return HRESULT_FROM_WIN32(GetLastError());
                }
                return hr;
            }
            else
            {
                dwRetries++;
                Sleep(200);
            }
        }
        else
        {
            bDeleted = true;
        }

    } while (!bDeleted);
    return S_OK;
}
