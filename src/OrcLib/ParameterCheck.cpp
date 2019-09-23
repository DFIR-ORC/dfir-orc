//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "ParameterCheck.h"
#include "SystemDetails.h"

#include "CaseInsensitive.h"

#include "Convert.h"

#include <filesystem>
#include <regex>

using namespace Orc;

static const auto MAX_CREATEDIR_RECURSIONS = 80;

HRESULT GetOutputDir_Internal(
    WCHAR* szOutputDir,
    DWORD cchOutputFileLengthInWCHARS,
    bool bAllowRecursiveFolderCreation,
    DWORD dwRecursionsLeft)
{
    DBG_UNREFERENCED_PARAMETER(cchOutputFileLengthInWCHARS);

    HRESULT hr = E_FAIL;
    DWORD dwAttr = GetFileAttributes(szOutputDir);

    if (dwAttr == INVALID_FILE_ATTRIBUTES)
    {
        DWORD dwErr = GetLastError();
        if (dwErr == ERROR_PATH_NOT_FOUND || dwErr == ERROR_FILE_NOT_FOUND)
        {
            if (!CreateDirectory(szOutputDir, NULL))
            {
                if (!bAllowRecursiveFolderCreation)
                    return HRESULT_FROM_WIN32(GetLastError());

                if (dwRecursionsLeft > 0)
                {
                    WCHAR path_buffer[_MAX_PATH];
                    WCHAR drive[_MAX_DRIVE];
                    WCHAR dir[_MAX_DIR];
                    WCHAR fname[_MAX_FNAME];
                    WCHAR ext[_MAX_EXT];
                    errno_t err;

                    size_t dwLen = wcslen(szOutputDir);
                    if (dwLen > MAX_PATH)
                        return E_INVALIDARG;
                    if (dwLen < 1)
                        return E_INVALIDARG;

                    if (szOutputDir[dwLen - 1] == L'\\')
                        szOutputDir[dwLen - 1] = L'\0';

                    err =
                        _wsplitpath_s(szOutputDir, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
                    if (err != 0)
                        return E_INVALIDARG;

                    err = _wmakepath_s(path_buffer, _MAX_PATH, drive, dir, NULL, NULL);
                    if (err != 0)
                        return E_INVALIDARG;

                    if (FAILED(hr = GetOutputDir_Internal(path_buffer, _MAX_PATH, true, --dwRecursionsLeft)))
                        return hr;

                    // We did not fail to create parent, now retry creating child
                    if (!CreateDirectory(szOutputDir, NULL))
                    {
                        return HRESULT_FROM_WIN32(GetLastError());
                    }
                    return S_OK;
                }
                else
                    HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
            }
            return S_OK;
        }
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if ((FILE_ATTRIBUTE_DIRECTORY & dwAttr) == 0)
        return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
    return S_OK;
}

HRESULT Orc::GetOutputDir(
    const WCHAR* szInputString,
    WCHAR* szOutputDir,
    DWORD cchOutputFileLengthInWCHARS,
    bool bAllowRecursiveFolderCreation)
{
    DWORD dwRequiredLen = ExpandEnvironmentStrings(szInputString, NULL, 0L);

    if (dwRequiredLen > cchOutputFileLengthInWCHARS)
        return ERROR_INSUFFICIENT_BUFFER;

    ZeroMemory(szOutputDir, cchOutputFileLengthInWCHARS * sizeof(WCHAR));
    ExpandEnvironmentStrings(szInputString, szOutputDir, cchOutputFileLengthInWCHARS);

    return GetOutputDir_Internal(
        szOutputDir, cchOutputFileLengthInWCHARS, bAllowRecursiveFolderCreation, MAX_CREATEDIR_RECURSIONS);
}

ORCLIB_API HRESULT Orc::GetOutputDir(const WCHAR* szInputString, std::wstring& strOutputDir, bool bCreateParentAsNeeded)
{
    HRESULT hr = E_FAIL;

    WCHAR szOutputDir[MAX_PATH];
    ZeroMemory(szOutputDir, sizeof(WCHAR) * MAX_PATH);
    if (FAILED(hr = GetOutputDir(szInputString, szOutputDir, MAX_PATH, bCreateParentAsNeeded)))
        return hr;
    strOutputDir.assign(szOutputDir);
    return S_OK;
}

HRESULT Orc::GetOutputFile(
    const WCHAR* szInputString,
    WCHAR* szOutputFile,
    DWORD cchOutputFileLengthInWCHARS,
    bool bCreateParentAsNeeded)
{
    DWORD dwRequiredLen = ExpandEnvironmentStrings(szInputString, NULL, 0L);

    if (dwRequiredLen > cchOutputFileLengthInWCHARS)
        return ERROR_INSUFFICIENT_BUFFER;

    ZeroMemory(szOutputFile, cchOutputFileLengthInWCHARS * sizeof(WCHAR));
    ExpandEnvironmentStrings(szInputString, szOutputFile, cchOutputFileLengthInWCHARS);

    if (bCreateParentAsNeeded)
    {
        WCHAR path_buffer[_MAX_PATH];
        WCHAR drive[_MAX_DRIVE];
        WCHAR dir[_MAX_DIR];
        WCHAR fname[_MAX_FNAME];
        WCHAR ext[_MAX_EXT];
        errno_t err;

        size_t dwLen = wcslen(szOutputFile);
        if (dwLen > MAX_PATH)
            return E_INVALIDARG;
        if (dwLen < 1)
            return E_INVALIDARG;

        if (szOutputFile[dwLen - 1] == L'\\')
            szOutputFile[dwLen - 1] = L'\0';

        err = _wsplitpath_s(szOutputFile, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
        if (err != 0)
            return E_INVALIDARG;

        err = _wmakepath_s(path_buffer, _MAX_PATH, drive, dir, NULL, NULL);
        if (err != 0)
            return E_INVALIDARG;

        return GetOutputDir_Internal(path_buffer, _MAX_PATH, true, MAX_CREATEDIR_RECURSIONS);
    }

    return S_OK;
}

ORCLIB_API HRESULT
Orc::GetOutputFile(const WCHAR* szInputString, std::wstring& strOutputFile, bool bCreateParentAsNeeded)
{
    HRESULT hr = E_FAIL;

    WCHAR szOutputFile[MAX_PATH];
    ZeroMemory(szOutputFile, sizeof(WCHAR) * MAX_PATH);
    if (FAILED(hr = GetOutputFile(szInputString, szOutputFile, MAX_PATH, bCreateParentAsNeeded)))
        return hr;
    strOutputFile.assign(szOutputFile);
    return S_OK;
}

HRESULT Orc::IsPathComplete(const WCHAR* szInputFile)
{
    WCHAR drive[_MAX_DRIVE];
    WCHAR dir[_MAX_DIR];
    WCHAR fname[_MAX_FNAME];
    WCHAR ext[_MAX_EXT];
    errno_t err;

    err = _wsplitpath_s(szInputFile, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
    if (err != 0)
        return E_INVALIDARG;

    if (wcslen(drive) == 0)
        return S_FALSE;

    return S_OK;
}

HRESULT Orc::IsFileName(const WCHAR* szInputFile)
{
    WCHAR drive[_MAX_DRIVE];
    WCHAR dir[_MAX_DIR];
    WCHAR fname[_MAX_FNAME];
    WCHAR ext[_MAX_EXT];
    errno_t err;

    err = _wsplitpath_s(szInputFile, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
    if (err != 0)
        return E_INVALIDARG;

    if (wcslen(drive) == 0 && wcslen(dir) == 0)
        return S_OK;

    return S_FALSE;
}

HRESULT Orc::GetInputFile(const WCHAR* szInputString, WCHAR* szInputFile, DWORD cchInputFileLengthInWCHARS)
{
    DWORD dwRequiredLen = ExpandEnvironmentStrings(szInputString, NULL, 0L);

    if (dwRequiredLen > cchInputFileLengthInWCHARS)
        return ERROR_INSUFFICIENT_BUFFER;

    ZeroMemory(szInputFile, cchInputFileLengthInWCHARS * sizeof(WCHAR));
    ExpandEnvironmentStrings(szInputString, szInputFile, cchInputFileLengthInWCHARS);

    DWORD dwAttr = GetFileAttributes(szInputFile);

    // Checking if file exists and is not a directory
    if (INVALID_FILE_ATTRIBUTES == dwAttr)
        return HRESULT_FROM_WIN32(GetLastError());
    if (FILE_ATTRIBUTE_DIRECTORY & dwAttr)
        return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);

    return S_OK;
}

ORCLIB_API HRESULT Orc::GetInputFile(const WCHAR* szInputString, std::wstring& strInputFile)
{
    HRESULT hr = E_FAIL;

    WCHAR szInputFile[MAX_PATH];
    ZeroMemory(szInputFile, sizeof(WCHAR) * MAX_PATH);
    if (FAILED(hr = GetInputFile(szInputString, szInputFile, MAX_PATH)))
        return hr;
    strInputFile.assign(szInputFile);
    return S_OK;
}

HRESULT Orc::GetInputDirectory(const WCHAR* szInputString, WCHAR* szInputDir, DWORD cchInputDirLengthInWCHARS)
{
    DWORD dwRequiredLen = ExpandEnvironmentStrings(szInputString, NULL, 0L);

    if (dwRequiredLen > cchInputDirLengthInWCHARS)
        return ERROR_INSUFFICIENT_BUFFER;

    ZeroMemory(szInputDir, cchInputDirLengthInWCHARS * sizeof(WCHAR));
    ExpandEnvironmentStrings(szInputString, szInputDir, cchInputDirLengthInWCHARS);

    DWORD dwAttr = GetFileAttributes(szInputDir);

    // Checking if file exists and is not a directory
    if (INVALID_FILE_ATTRIBUTES == dwAttr)
        return HRESULT_FROM_WIN32(GetLastError());
    if (FILE_ATTRIBUTE_DIRECTORY & dwAttr)
        return S_OK;

    return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
}

ORCLIB_API HRESULT Orc::GetInputDirectory(const WCHAR* szInputString, std::wstring& strInputDir)
{
    HRESULT hr = E_FAIL;

    WCHAR szInputDir[MAX_PATH];
    ZeroMemory(szInputDir, sizeof(WCHAR) * MAX_PATH);
    if (FAILED(hr = GetInputDirectory(szInputString, szInputDir, MAX_PATH)))
        return hr;
    strInputDir.assign(szInputDir);
    return S_OK;
}

HRESULT Orc::VerifyFileExists(const WCHAR* szInputFile)
{
    DWORD dwAttr = GetFileAttributes(szInputFile);

    // Checking if file exists and is not a directory
    if (INVALID_FILE_ATTRIBUTES == dwAttr)
        return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
    if (FILE_ATTRIBUTE_DIRECTORY & dwAttr)
        return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);

    return S_OK;
}

HRESULT Orc::VerifyDirectoryExists(const WCHAR* szInputDir)
{
    DWORD dwAttr = GetFileAttributes(szInputDir);

    // Checking if file exists and is not a directory
    if (INVALID_FILE_ATTRIBUTES == dwAttr)
        return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
    if (!(FILE_ATTRIBUTE_DIRECTORY & dwAttr))
        return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);

    return S_OK;
}

HRESULT Orc::VerifyFileIsBinary(const WCHAR* szInputFile)
{
    DWORD dwType = 0;

    if (!GetBinaryType(szInputFile, &dwType))
        return S_FALSE;

    if (dwType != SCS_32BIT_BINARY && dwType != SCS_64BIT_BINARY && dwType != SCS_WOW_BINARY)
        return S_FALSE;

    WORD wArch;
    if (FAILED(SystemDetails::GetArchitecture(wArch)))
        return S_FALSE;

    switch (wArch)
    {
        case PROCESSOR_ARCHITECTURE_AMD64:
            if (dwType == SCS_32BIT_BINARY || dwType == SCS_64BIT_BINARY)
                return S_OK;
            break;
        case PROCESSOR_ARCHITECTURE_INTEL:
            if (dwType == SCS_32BIT_BINARY || dwType == SCS_WOW_BINARY)
                return S_OK;
            break;
        default:
            return S_FALSE;
    }
    return S_FALSE;
}

ORCLIB_API HRESULT Orc::GetProcessModuleDirectory(WCHAR* szDirectory, DWORD cchDirectoryLengthInWCHARS)
{
    WCHAR szProcessFileName[MAX_PATH];

    if (!GetModuleFileName(NULL, szProcessFileName, MAX_PATH))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return GetDirectoryForFile(szProcessFileName, szDirectory, cchDirectoryLengthInWCHARS);
}

ORCLIB_API HRESULT Orc::GetProcessModuleFileName(WCHAR* szFileName, DWORD cchFileNameLengthInWCHARS)
{
    WCHAR szProcessFileName[MAX_PATH];

    if (!GetModuleFileName(NULL, szProcessFileName, MAX_PATH))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return GetFileNameForFile(szProcessFileName, szFileName, cchFileNameLengthInWCHARS);
}

ORCLIB_API HRESULT Orc::GetProcessModuleFullPath(WCHAR* szFullPath, DWORD cchFullPathLengthInWCHARS)
{
    if (!GetModuleFileName(NULL, szFullPath, cchFullPathLengthInWCHARS))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

ORCLIB_API HRESULT
Orc::GetDirectoryForFile(const WCHAR* szInputString, WCHAR* szDirectory, DWORD cchDirectoryLengthInWCHARS)
{
    WCHAR path_buffer[_MAX_PATH];
    WCHAR drive[_MAX_DRIVE];
    WCHAR dir[_MAX_DIR];
    WCHAR fname[_MAX_FNAME];
    WCHAR ext[_MAX_EXT];
    errno_t err;

    size_t dwLen = wcslen(szInputString);

    if (dwLen > MAX_PATH)
        return E_INVALIDARG;
    if (dwLen < 1)
        return E_INVALIDARG;

    wcscpy_s(path_buffer, szInputString);
    if (szInputString[dwLen - 1] == L'\\')
        path_buffer[dwLen - 1] = L'\0';

    err = _wsplitpath_s(path_buffer, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
    if (err != 0)
        return E_INVALIDARG;
    ZeroMemory(path_buffer, MAX_PATH * sizeof(WCHAR));

    err = _wmakepath_s(szDirectory, cchDirectoryLengthInWCHARS, drive, dir, NULL, NULL);

    return S_OK;
}

ORCLIB_API HRESULT
Orc::GetFileNameForFile(const WCHAR* szInputString, WCHAR* szFileName, DWORD cchFileNameLengthInWCHARS)
{
    WCHAR path_buffer[_MAX_PATH];
    WCHAR drive[_MAX_DRIVE];
    WCHAR dir[_MAX_DIR];
    WCHAR fname[_MAX_FNAME];
    WCHAR ext[_MAX_EXT];
    errno_t err;

    size_t dwLen = wcslen(szInputString);
    if (dwLen < 1)
        return E_INVALIDARG;

    if (dwLen > MAX_PATH)
        return E_INVALIDARG;

    wcscpy_s(path_buffer, szInputString);
    if (szInputString[dwLen - 1] == L'\\')
        path_buffer[dwLen - 1] = L'\0';

    err = _wsplitpath_s(path_buffer, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
    if (err != 0)
        return E_INVALIDARG;
    ZeroMemory(path_buffer, MAX_PATH * sizeof(WCHAR));

    err = _wmakepath_s(szFileName, cchFileNameLengthInWCHARS, NULL, NULL, fname, ext);
    if (err != 0)
        return E_INVALIDARG;
    return S_OK;
}

ORCLIB_API HRESULT
Orc::GetExtensionForFile(const WCHAR* szInputString, WCHAR* szExtension, DWORD cchExtensionLengthInWCHARS)
{
    WCHAR path_buffer[_MAX_PATH];

    errno_t err;

    size_t dwLen = wcslen(szInputString);
    if (dwLen > MAX_PATH)
        return E_INVALIDARG;
    if (dwLen < 1)
        return E_INVALIDARG;

    wcscpy_s(path_buffer, szInputString);
    if (szInputString[dwLen - 1] == L'\\')
        path_buffer[dwLen - 1] = L'\0';

    err = _wsplitpath_s(path_buffer, NULL, 0L, NULL, 0L, NULL, 0L, szExtension, cchExtensionLengthInWCHARS);
    if (err != 0)
        return E_INVALIDARG;
    return S_OK;
}

ORCLIB_API HRESULT
Orc::GetBaseNameForFile(const WCHAR* szInputString, WCHAR* szBaseName, DWORD cchBaseNameLengthInWCHARS)
{
    WCHAR path_buffer[_MAX_PATH];

    errno_t err;

    size_t dwLen = wcslen(szInputString);
    if (dwLen > MAX_PATH)
        return E_INVALIDARG;
    if (dwLen < 1)
        return E_INVALIDARG;

    wcscpy_s(path_buffer, szInputString);
    if (szInputString[dwLen - 1] == L'\\')
        path_buffer[dwLen - 1] = L'\0';

    err = _wsplitpath_s(path_buffer, NULL, 0L, NULL, 0L, szBaseName, cchBaseNameLengthInWCHARS, NULL, 0L);
    if (err != 0)
        return E_INVALIDARG;
    return S_OK;
}

ORCLIB_API HRESULT Orc::GetFileNameAndDirectoryForFile(
    const WCHAR* szInputString,
    WCHAR* szDirectory,
    DWORD cchDirectoryLengthInWCHARS,
    WCHAR* szFileName,
    DWORD cchFileNameLengthInWCHARS)
{
    WCHAR path_buffer[_MAX_PATH];
    WCHAR drive[_MAX_DRIVE];
    WCHAR dir[_MAX_DIR];
    WCHAR fname[_MAX_FNAME];
    WCHAR ext[_MAX_EXT];
    errno_t err;

    size_t dwLen = wcslen(szInputString);

    if (dwLen > MAX_PATH)
        return E_INVALIDARG;
    if (dwLen < 1)
        return E_INVALIDARG;

    wcscpy_s(path_buffer, szInputString);
    if (szInputString[dwLen - 1] == L'\\')
        path_buffer[dwLen - 1] = L'\0';

    err = _wsplitpath_s(path_buffer, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
    if (err != 0)
        return E_INVALIDARG;
    ZeroMemory(path_buffer, MAX_PATH * sizeof(WCHAR));

    err = _wmakepath_s(szFileName, cchFileNameLengthInWCHARS, NULL, NULL, fname, ext);
    if (err != 0)
        return E_INVALIDARG;

    err = _wmakepath_s(szDirectory, cchDirectoryLengthInWCHARS, drive, dir, NULL, NULL);
    if (err != 0)
        return E_INVALIDARG;

    return S_OK;
}

HRESULT Orc::GetOutputCab(
    const WCHAR* szInputString,
    WCHAR* szOutputCab,
    DWORD cchOutputCabLengthInWCHARS,
    bool bAllowRecursiveFolderCreation)
{
    DWORD dwRequiredLen = ExpandEnvironmentStrings(szInputString, NULL, 0L);

    if (dwRequiredLen > cchOutputCabLengthInWCHARS)
        return ERROR_INSUFFICIENT_BUFFER;

    ZeroMemory(szOutputCab, cchOutputCabLengthInWCHARS * sizeof(WCHAR));
    ExpandEnvironmentStrings(szInputString, szOutputCab, cchOutputCabLengthInWCHARS);

    DWORD dwAttr = GetFileAttributes(szOutputCab);
    if (INVALID_FILE_ATTRIBUTES == dwAttr)
    {
        if (bAllowRecursiveFolderCreation)
        {
            WCHAR path_buffer[_MAX_PATH];
            WCHAR drive[_MAX_DRIVE];
            WCHAR dir[_MAX_DIR];
            WCHAR fname[_MAX_FNAME];
            WCHAR ext[_MAX_EXT];
            errno_t err;

            size_t dwLen = wcslen(szOutputCab);
            if (dwLen > MAX_PATH)
                return E_INVALIDARG;
            if (dwLen < 1)
                return E_INVALIDARG;

            if (szOutputCab[dwLen - 1] == L'\\')
                szOutputCab[dwLen - 1] = L'\0';

            err = _wsplitpath_s(szOutputCab, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
            if (err != 0)
                return E_INVALIDARG;

            err = _wmakepath_s(path_buffer, _MAX_PATH, drive, dir, NULL, NULL);
            if (err != 0)
                return E_INVALIDARG;

            return GetOutputDir_Internal(path_buffer, _MAX_PATH, true, MAX_CREATEDIR_RECURSIONS);
        }
        return S_OK;
    }
    if (FILE_ATTRIBUTE_DIRECTORY & dwAttr)
        return HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    return S_OK;
}

ORCLIB_API HRESULT Orc::GetOutputCab(const WCHAR* szInputString, std::wstring& strOutputCab, bool bCreateParentAsNeeded)
{
    HRESULT hr = E_FAIL;

    WCHAR szOutputCab[MAX_PATH];
    ZeroMemory(szOutputCab, sizeof(WCHAR) * MAX_PATH);
    if (FAILED(hr = GetOutputCab(szInputString, szOutputCab, MAX_PATH, bCreateParentAsNeeded)))
        return hr;
    strOutputCab.assign(szOutputCab);
    return S_OK;
}

HRESULT Orc::GetFileSizeFromArg(const WCHAR* pSize, LARGE_INTEGER& size)
{
    size.HighPart = 0L;
    size.LowPart = 0L;

    DWORD dwLen = (DWORD)wcslen(pSize);

    if (dwLen > 2)
        if ((pSize[dwLen - 1] == L'B' || pSize[dwLen - 1] == L'b')
            && (pSize[dwLen - 2] == L'G' || pSize[dwLen - 2] == L'g' || pSize[dwLen - 2] == L'M'
                || pSize[dwLen - 2] == L'm' || pSize[dwLen - 2] == L'K' || pSize[dwLen - 2] == L'k'))
        {
            dwLen--;
        }
    if (dwLen > MAX_PATH)
    {
        return E_INVALIDARG;
    }

    WCHAR szTemp[MAX_PATH];
    wcscpy_s(szTemp, pSize);

    DWORD dwMultiplier = 1;
    switch (szTemp[dwLen - 1])
    {
        case L'B':
        case L'b':
            dwMultiplier = 1;
            szTemp[dwLen - 1] = L'\0';
            break;
        case L'K':
        case L'k':
            dwMultiplier = 1024;
            szTemp[dwLen - 1] = L'\0';
            break;
        case L'M':
        case L'm':
            dwMultiplier = 1024 * 1024;
            szTemp[dwLen - 1] = L'\0';
            break;
        case L'G':
        case L'g':
            dwMultiplier = 1024 * 1024 * 1024;
            szTemp[dwLen - 1] = L'\0';
            break;
    }

    size.QuadPart = _wtoi64(szTemp) * dwMultiplier;
    if (errno == ERANGE)
        return E_INVALIDARG;

    return S_OK;
}

HRESULT Orc::GetPercentageFromArg(const WCHAR* pStr, DWORD& value)
{
    if (pStr == nullptr)
        return E_POINTER;

    std::wstring_view str(pStr);

    if (str.back() == L'%')
    {
        str.remove_suffix(1);
    }
    auto _value = _wtoi(pStr);

    if (_value > 100)
        return E_INVALIDARG;

    if (errno == ERANGE || value == 0)
        return E_INVALIDARG;

    try
    {
        value = ConvertTo<DWORD>(_value);
    }
    catch (const std::overflow_error&)
    {
        return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT Orc::GetIntegerFromArg(const WCHAR* pSize, LARGE_INTEGER& size)
{
    size.HighPart = 0L;
    size.LowPart = 0L;

    if (pSize == nullptr)
        return E_POINTER;

    size.QuadPart = _wtoi64(pSize);

    if (size.QuadPart == 0LL)
    {
        const WCHAR* pCur = pSize;
        while (*pCur)
        {
            if (*pCur != L'0')
                return E_INVALIDARG;
            pCur++;
        }
    }

    if (errno == ERANGE || errno == EINVAL)
        return E_INVALIDARG;

    return S_OK;
}

HRESULT Orc::GetIntegerFromArg(const WCHAR* pStr, size_t& size)
{
    if (pStr == nullptr)
        return E_POINTER;

    auto _size = _wtoi(pStr);

    if (errno == ERANGE || _size == 0)
        return E_INVALIDARG;
    try
    {
        size = ConvertTo<size_t>(_size);
    }
    catch (const std::overflow_error&)
    {
        return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT Orc::GetIntegerFromArg(const WCHAR* pStr, DWORD& value)
{
    if (pStr == nullptr)
        return E_POINTER;

    auto _value = _wtoi(pStr);

    if (errno == ERANGE || _value == 0)
        return E_INVALIDARG;

    try
    {
        value = ConvertTo<DWORD>(_value);
    }
    catch (const std::overflow_error&)
    {
        return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT Orc::GetIntegerFromHexaString(const WCHAR* pszStr, DWORD& result)
{
    result = 0L;
    if (pszStr == NULL)
        return E_POINTER;
    if (pszStr[0] != L'0' && (pszStr[1] != 'X' || pszStr[1] != 'x'))
        return E_INVALIDARG;

    DWORD dwLen = (DWORD)wcslen(pszStr);

    if (dwLen > 10)
        return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);

    for (unsigned int i = 2; i < dwLen; i++)
    {
        if (pszStr[i] >= L'0' && pszStr[i] <= L'9')
            continue;
        if (pszStr[i] >= L'A' && pszStr[i] <= L'F')
            continue;
        if (pszStr[i] >= L'a' && pszStr[i] <= L'f')
            continue;
        return E_INVALIDARG;
    }

    DWORD mult = 1;
    for (int j = dwLen - 1; j > 1; j--)
    {
        if (pszStr[j] >= L'0' && pszStr[j] <= L'9')
            result += (pszStr[j] - L'0') * mult;
        else if (pszStr[j] >= L'A' && pszStr[j] <= L'F')
            result += (pszStr[j] - L'A' + 10) * mult;
        else if (pszStr[j] >= L'a' && pszStr[j] <= L'f')
            result += (pszStr[j] - L'a' + 10) * mult;
        mult *= 16;
    }
    return S_OK;
}

HRESULT Orc::GetIntegerFromHexaString(const CHAR* pszStr, DWORD& result)
{
    result = 0L;
    if (pszStr == NULL)
        return E_POINTER;
    if (pszStr[0] != '0' && (pszStr[1] != 'X' || pszStr[1] != 'x'))
        return E_INVALIDARG;

    DWORD dwLen = (DWORD)strlen(pszStr);

    if (dwLen > 10)
        return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);

    for (unsigned int i = 2; i < dwLen; i++)
    {
        if (pszStr[i] >= '0' && pszStr[i] <= '9')
            continue;
        if (pszStr[i] >= 'A' && pszStr[i] <= 'F')
            continue;
        if (pszStr[i] >= 'a' && pszStr[i] <= 'f')
            continue;
        return E_INVALIDARG;
    }

    DWORD mult = 1;
    for (int j = dwLen - 1; j > 1; j--)
    {
        if (pszStr[j] >= '0' && pszStr[j] <= '9')
            result += (pszStr[j] - '0') * mult;
        else if (pszStr[j] >= 'A' && pszStr[j] <= 'F')
            result += (pszStr[j] - 'A' + 10) * mult;
        else if (pszStr[j] >= 'a' && pszStr[j] <= 'f')
            result += (pszStr[j] - 'a' + 10) * mult;
        mult *= 16;
    }
    return S_OK;
}

HRESULT Orc::GetIntegerFromHexaString(const WCHAR* pszStr, LARGE_INTEGER& result)
{
    result.HighPart = 0L;
    result.LowPart = 0L;

    if (pszStr == NULL)
        return E_POINTER;
    if (pszStr[0] != L'0' || (pszStr[1] != L'X' && pszStr[1] != L'x'))
        return E_INVALIDARG;

    if (wcslen(pszStr) != 18)
        return E_INVALIDARG;

    for (int i = 2; i < 18; i++)
    {
        if (pszStr[i] >= L'0' && pszStr[i] <= L'9')
            continue;
        if (pszStr[i] >= L'A' && pszStr[i] <= L'F')
            continue;
        if (pszStr[i] >= L'a' && pszStr[i] <= L'f')
            continue;
        return E_INVALIDARG;
    }

    DWORDLONG mult = 1;
    for (int j = 17; j > 1; j--)
    {
        if (pszStr[j] >= L'0' && pszStr[j] <= L'9')
            result.QuadPart += (pszStr[j] - L'0') * mult;
        else if (pszStr[j] >= L'A' && pszStr[j] <= L'F')
            result.QuadPart += (pszStr[j] - L'A' + 10) * mult;
        else if (pszStr[j] >= L'a' && pszStr[j] <= L'f')
            result.QuadPart += (pszStr[j] - L'a' + 10) * mult;

        mult *= 16;
    }
    return S_OK;
}

HRESULT Orc::GetIntegerFromHexaString(const CHAR* pszStr, LARGE_INTEGER& result)
{
    result.HighPart = 0L;
    result.LowPart = 0L;

    if (pszStr == NULL)
        return E_POINTER;
    if (pszStr[0] != '0' && (pszStr[1] != 'X' || pszStr[1] != 'x'))
        return E_INVALIDARG;
    if (strlen(pszStr) != 18)
        return E_INVALIDARG;

    for (int i = 2; i < 18; i++)
    {
        if (pszStr[i] >= '0' && pszStr[i] <= '9')
            continue;
        if (pszStr[i] >= 'A' && pszStr[i] <= 'F')
            continue;
        if (pszStr[i] >= 'a' && pszStr[i] <= 'f')
            continue;
        return E_INVALIDARG;
    }

    DWORDLONG mult = 1;
    for (int j = 17; j > 1; j--)
    {
        if (pszStr[j] >= '0' && pszStr[j] <= '9')
            result.QuadPart += (pszStr[j] - '0') * mult;
        else if (pszStr[j] >= 'A' && pszStr[j] <= 'F')
            result.QuadPart += (pszStr[j] - 'A' + 10) * mult;
        else if (pszStr[j] >= 'a' && pszStr[j] <= 'f')
            result.QuadPart += (pszStr[j] - 'a' + 10) * mult;
        mult *= 16;
    }
    return S_OK;
}

HRESULT
Orc::GetBytesFromHexaString(const WCHAR* pszStr, DWORD dwStrLen, BYTE* pBytes, DWORD dwBytesLen, DWORD* pdwBytesLen)
{
    if (pszStr == NULL)
        return E_POINTER;
    if (dwStrLen % 2)
        return E_INVALIDARG;

    if (dwStrLen == 0)
    {
        if (pdwBytesLen)
            *pdwBytesLen = 0L;
        ZeroMemory(pBytes, dwBytesLen);
    }
    DWORD dwStartAt = 0L;
    if (pszStr[0] == L'0' && (pszStr[1] == L'x' || pszStr[1] == L'X'))
    {
        dwStartAt = 2;
        if (dwBytesLen < (dwStrLen - 2) / 2)
            return E_INVALIDARG;
    }
    else if (dwBytesLen < dwStrLen / 2)
        return E_INVALIDARG;

    for (DWORD i = dwStartAt; i < dwStrLen; i++)
    {
        if (pszStr[i] >= L'0' && pszStr[i] <= L'9')
            continue;
        if (pszStr[i] >= L'A' && pszStr[i] <= L'F')
            continue;
        if (pszStr[i] >= L'a' && pszStr[i] <= L'f')
            continue;
        return E_INVALIDARG;
    }

    ZeroMemory(pBytes, dwBytesLen);

    DWORD dwArrayIndex = 0L;
    for (unsigned int i = dwStartAt; i < dwStrLen - 1; i += 2)
    {
        if (pszStr[i + 1] >= L'0' && pszStr[i + 1] <= L'9')
            pBytes[dwArrayIndex] = (BYTE)pszStr[i + 1] - L'0';
        else if (pszStr[i + 1] >= L'A' && pszStr[i + 1] <= L'F')
            pBytes[dwArrayIndex] = (BYTE)pszStr[i + 1] - L'A' + 10;
        else if (pszStr[i + 1] >= L'a' && pszStr[i + 1] <= L'f')
            pBytes[dwArrayIndex] = (BYTE)pszStr[i + 1] - L'a' + 10;

        if (pszStr[i] >= L'0' && pszStr[i] <= L'9')
            pBytes[dwArrayIndex] |= (BYTE)((pszStr[i] - L'0') << 4);
        else if (pszStr[i] >= L'A' && pszStr[i] <= L'F')
            pBytes[dwArrayIndex] |= (BYTE)((pszStr[i] - L'A' + 10) << 4);
        else if (pszStr[i] >= L'a' && pszStr[i] <= L'f')
            pBytes[dwArrayIndex] |= (BYTE)((pszStr[i] - L'a' + 10) << 4);

        dwArrayIndex++;
    }
    if (pdwBytesLen != nullptr)
        *pdwBytesLen = dwArrayIndex;
    return S_OK;
}

HRESULT
Orc::GetBytesFromHexaString(const CHAR* pszStr, DWORD dwStrLen, BYTE* pBytes, DWORD dwBytesLen, DWORD* pdwBytesLen)
{
    if (pszStr == NULL)
        return E_POINTER;
    if (dwStrLen % 2)
        return E_INVALIDARG;

    if (dwStrLen == 0)
    {
        if (pdwBytesLen)
            *pdwBytesLen = 0L;
        ZeroMemory(pBytes, dwBytesLen);
    }
    DWORD dwStartAt = 0L;
    if (pszStr[0] == '0' && (pszStr[1] == 'x' || pszStr[1] == 'X'))
    {
        dwStartAt = 2;
        if (dwBytesLen < (dwStrLen - 2) / 2)
            return E_INVALIDARG;
    }
    else if (dwBytesLen < dwStrLen / 2)
        return E_INVALIDARG;

    for (DWORD i = dwStartAt; i < dwStrLen; i++)
    {
        if (pszStr[i] >= '0' && pszStr[i] <= '9')
            continue;
        if (pszStr[i] >= 'A' && pszStr[i] <= 'F')
            continue;
        if (pszStr[i] >= 'a' && pszStr[i] <= 'f')
            continue;
        return E_INVALIDARG;
    }

    ZeroMemory(pBytes, dwBytesLen);

    DWORD dwArrayIndex = 0L;
    for (unsigned int i = dwStartAt; i < dwStrLen - 1; i += 2)
    {
        if (pszStr[i + 1] >= '0' && pszStr[i + 1] <= '9')
            pBytes[dwArrayIndex] = (BYTE)pszStr[i + 1] - '0';
        else if (pszStr[i + 1] >= 'A' && pszStr[i + 1] <= 'F')
            pBytes[dwArrayIndex] = (BYTE)pszStr[i + 1] - 'A' + 10;
        else if (pszStr[i + 1] >= 'a' && pszStr[i + 1] <= 'f')
            pBytes[dwArrayIndex] = (BYTE)pszStr[i + 1] - 'a' + 10;

        if (pszStr[i] >= '0' && pszStr[i] <= '9')
            pBytes[dwArrayIndex] |= (BYTE)((pszStr[i] - '0') << 4);
        else if (pszStr[i] >= 'A' && pszStr[i] <= 'F')
            pBytes[dwArrayIndex] |= (BYTE)((pszStr[i] - 'A' + 10) << 4);
        else if (pszStr[i] >= 'a' && pszStr[i] <= 'f')
            pBytes[dwArrayIndex] |= (BYTE)((pszStr[i] - 'a' + 10) << 4);

        dwArrayIndex++;
    }
    if (pdwBytesLen != nullptr)
        *pdwBytesLen = dwArrayIndex;
    return S_OK;
}

HRESULT Orc::GetBytesFromHexaString(const WCHAR* pszStr, DWORD dwStrLen, CBinaryBuffer& buffer)
{
    if (pszStr == NULL)
        return E_POINTER;
    if (dwStrLen % 2)
        return E_INVALIDARG;

    DWORD dwStartAt = 0L;

    if (pszStr[0] == L'0' && (pszStr[1] == L'x' || pszStr[1] == L'X'))
    {
        dwStartAt = 2;
        buffer.SetCount((dwStrLen - 2) / 2);
    }
    else
        buffer.SetCount(dwStrLen / 2);

    for (DWORD i = dwStartAt; i < dwStrLen; i++)
    {
        if (pszStr[i] >= L'0' && pszStr[i] <= L'9')
            continue;
        if (pszStr[i] >= L'A' && pszStr[i] <= L'F')
            continue;
        if (pszStr[i] >= L'a' && pszStr[i] <= L'f')
            continue;
        return E_INVALIDARG;
    }

    buffer.ZeroMe();

    DWORD dwArrayIndex = 0L;
    for (unsigned int i = dwStartAt; i < dwStrLen - 1; i += 2)
    {
        if (pszStr[i + 1] >= L'0' && pszStr[i + 1] <= L'9')
            buffer[dwArrayIndex] = (BYTE)pszStr[i + 1] - L'0';
        else if (pszStr[i + 1] >= L'A' && pszStr[i + 1] <= L'F')
            buffer[dwArrayIndex] = (BYTE)pszStr[i + 1] - L'A' + 10;
        else if (pszStr[i + 1] >= L'a' && pszStr[i + 1] <= L'f')
            buffer[dwArrayIndex] = (BYTE)pszStr[i + 1] - L'a' + 10;

        if (pszStr[i] >= L'0' && pszStr[i] <= L'9')
            buffer[dwArrayIndex] |= (BYTE)((pszStr[i] - L'0') << 4);
        else if (pszStr[i] >= L'A' && pszStr[i] <= L'F')
            buffer[dwArrayIndex] |= (BYTE)((pszStr[i] - L'A' + 10) << 4);
        else if (pszStr[i] >= L'a' && pszStr[i] <= L'f')
            buffer[dwArrayIndex] |= (BYTE)((pszStr[i] - L'a' + 10) << 4);

        dwArrayIndex++;
    }
    return S_OK;
}

HRESULT Orc::GetBytesFromHexaString(const CHAR* pszStr, DWORD dwStrLen, CBinaryBuffer& buffer)
{
    if (pszStr == NULL)
        return E_POINTER;
    if (dwStrLen % 2)
        return E_INVALIDARG;

    DWORD dwStartAt = 0L;

    if (pszStr[0] == '0' && (pszStr[1] == 'x' || pszStr[1] == 'X'))
    {
        dwStartAt = 2;
        buffer.SetCount((dwStrLen - 2) / 2);
    }
    else
        buffer.SetCount(dwStrLen / 2);

    for (DWORD i = dwStartAt; i < dwStrLen; i++)
    {
        if (pszStr[i] >= '0' && pszStr[i] <= '9')
            continue;
        if (pszStr[i] >= 'A' && pszStr[i] <= 'F')
            continue;
        if (pszStr[i] >= 'a' && pszStr[i] <= 'f')
            continue;
        return E_INVALIDARG;
    }

    buffer.ZeroMe();

    DWORD dwArrayIndex = 0L;
    for (unsigned int i = dwStartAt; i < dwStrLen - 1; i += 2)
    {
        if (pszStr[i + 1] >= '0' && pszStr[i + 1] <= '9')
            buffer[dwArrayIndex] = (BYTE)pszStr[i + 1] - '0';
        else if (pszStr[i + 1] >= 'A' && pszStr[i + 1] <= 'F')
            buffer[dwArrayIndex] = (BYTE)pszStr[i + 1] - 'A' + 10;
        else if (pszStr[i + 1] >= 'a' && pszStr[i + 1] <= 'f')
            buffer[dwArrayIndex] = (BYTE)pszStr[i + 1] - 'a' + 10;

        if (pszStr[i] >= '0' && pszStr[i] <= '9')
            buffer[dwArrayIndex] |= (BYTE)((pszStr[i] - '0') << 4);
        else if (pszStr[i] >= 'A' && pszStr[i] <= 'F')
            buffer[dwArrayIndex] |= (BYTE)((pszStr[i] - 'A' + 10) << 4);
        else if (pszStr[i] >= 'a' && pszStr[i] <= 'f')
            buffer[dwArrayIndex] |= (BYTE)((pszStr[i] - 'a' + 10) << 4);

        dwArrayIndex++;
    }
    return S_OK;
}

HRESULT GetSubFormat(
    const WCHAR* szFormat,
    unsigned int& formatIndex,
    const WCHAR* szString,
    unsigned int& stringIndex,
    unsigned short& result)
{
    WCHAR String[5];
    DWORD locIndex = 0L;
    ZeroMemory(String, 5 * sizeof(WCHAR));

    WCHAR currentChar = szFormat[formatIndex];

    do
    {
        formatIndex++;
    } while (szFormat[formatIndex] == currentChar);

    while ((szString[stringIndex] >= L'0' && szString[stringIndex] <= L'9') && locIndex < 5)
    {
        String[locIndex] = szString[stringIndex];
        stringIndex++;
        locIndex++;
    }

    try
    {
        result = Orc::ConvertTo<unsigned short>(_wtoi(String));
    }
    catch (const std::overflow_error&)
    {
        return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT Orc::GetDateFromString(const WCHAR* szFormat, const WCHAR* szString, FILETIME& result)
{
    HRESULT hr = E_FAIL;
    SYSTEMTIME time;

    DWORD dwFormatLength = (DWORD)wcslen(szFormat);
    DWORD dwStringLength = (DWORD)wcslen(szString);

    unsigned int stringIndex = 0;
    unsigned int formatIndex = 0;

    do
    {
        switch (szFormat[formatIndex])
        {
            case L'Y':
            case L'y':
                if (FAILED(hr = GetSubFormat(szFormat, formatIndex, szString, stringIndex, time.wYear)))
                    return hr;
                break;
            case L'M':
                if (FAILED(hr = GetSubFormat(szFormat, formatIndex, szString, stringIndex, time.wMonth)))
                    return hr;
                break;
            case L'D':
            case L'd':
                if (FAILED(hr = GetSubFormat(szFormat, formatIndex, szString, stringIndex, time.wDay)))
                    return hr;
                break;
            case L'H':
            case L'h':
                if (FAILED(hr = GetSubFormat(szFormat, formatIndex, szString, stringIndex, time.wHour)))
                    return hr;
                break;
            case L'm':
                if (FAILED(hr = GetSubFormat(szFormat, formatIndex, szString, stringIndex, time.wMinute)))
                    return hr;
                break;
            case L'S':
            case L's':
                if (FAILED(hr = GetSubFormat(szFormat, formatIndex, szString, stringIndex, time.wSecond)))
                    return hr;
                break;
            case L'0':
                if (FAILED(hr = GetSubFormat(szFormat, formatIndex, szString, stringIndex, time.wMilliseconds)))
                    return hr;
                break;
            case L'l':
                if (szFormat[formatIndex + 1] == L'x')
                {
                    formatIndex++;
                    if (FAILED(hr = GetSubFormat(szFormat, formatIndex, szString, stringIndex, time.wMilliseconds)))
                        return hr;
                }
                break;
            default:
                if (iswspace(szFormat[formatIndex]))
                {
                    formatIndex++;
                    while (iswspace(szString[stringIndex]) && stringIndex < dwStringLength)
                    {
                        stringIndex++;
                    }
                }
                else if (szFormat[formatIndex] == szString[stringIndex])
                {
                    stringIndex++;
                    formatIndex++;
                }
                else
                    return E_INVALIDARG;
                break;
        }
    } while (formatIndex < dwFormatLength);

    FILETIME ftime;

    ZeroMemory(&result, sizeof(result));

    if (!SystemTimeToFileTime(&time, &ftime))
        return HRESULT_FROM_WIN32(GetLastError());

    result = ftime;
    return S_OK;
}
