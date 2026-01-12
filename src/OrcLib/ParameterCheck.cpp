//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "ParameterCheck.h"
#include "SystemDetails.h"
#include "BinaryBuffer.h"
#include "WideAnsi.h"

#include "CaseInsensitive.h"

#include "Convert.h"

#include <filesystem>
#include <regex>
#include <limits>
#include <cerrno>
#include <cwchar>
#include <errno.h>

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
                    WCHAR path_buffer[ORC_MAX_PATH];
                    WCHAR drive[_MAX_DRIVE];
                    WCHAR dir[_MAX_DIR];
                    WCHAR fname[_MAX_FNAME];
                    WCHAR ext[_MAX_EXT];
                    errno_t err;

                    size_t dwLen = wcslen(szOutputDir);
                    if (dwLen > ORC_MAX_PATH)
                        return E_INVALIDARG;
                    if (dwLen < 1)
                        return E_INVALIDARG;

                    if (szOutputDir[dwLen - 1] == L'\\')
                        szOutputDir[dwLen - 1] = L'\0';

                    err =
                        _wsplitpath_s(szOutputDir, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
                    if (err != 0)
                        return E_INVALIDARG;

                    err = _wmakepath_s(path_buffer, ORC_MAX_PATH, drive, dir, NULL, NULL);
                    if (err != 0)
                        return E_INVALIDARG;

                    if (FAILED(hr = GetOutputDir_Internal(path_buffer, ORC_MAX_PATH, true, --dwRecursionsLeft)))
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

HRESULT Orc::GetOutputDir(const WCHAR* szInputString, std::wstring& strOutputDir, bool bCreateParentAsNeeded)
{
    HRESULT hr = E_FAIL;

    WCHAR szOutputDir[ORC_MAX_PATH];
    ZeroMemory(szOutputDir, sizeof(WCHAR) * ORC_MAX_PATH);
    if (FAILED(hr = GetOutputDir(szInputString, szOutputDir, ORC_MAX_PATH, bCreateParentAsNeeded)))
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
        WCHAR path_buffer[ORC_MAX_PATH];
        WCHAR drive[_MAX_DRIVE];
        WCHAR dir[_MAX_DIR];
        WCHAR fname[_MAX_FNAME];
        WCHAR ext[_MAX_EXT];
        errno_t err;

        size_t dwLen = wcslen(szOutputFile);
        if (dwLen > ORC_MAX_PATH)
            return E_INVALIDARG;
        if (dwLen < 1)
            return E_INVALIDARG;

        if (szOutputFile[dwLen - 1] == L'\\')
            szOutputFile[dwLen - 1] = L'\0';

        err = _wsplitpath_s(szOutputFile, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
        if (err != 0)
            return E_INVALIDARG;

        err = _wmakepath_s(path_buffer, ORC_MAX_PATH, drive, dir, NULL, NULL);
        if (err != 0)
            return E_INVALIDARG;

        if (wcslen(path_buffer) == 0L)
            return S_OK;  // this is a relative path (only a filename)

        return GetOutputDir_Internal(path_buffer, ORC_MAX_PATH, true, MAX_CREATEDIR_RECURSIONS);
    }

    return S_OK;
}

HRESULT
Orc::GetOutputFile(const WCHAR* szInputString, std::wstring& strOutputFile, bool bCreateParentAsNeeded)
{
    HRESULT hr = E_FAIL;

    WCHAR szOutputFile[ORC_MAX_PATH];
    ZeroMemory(szOutputFile, sizeof(WCHAR) * ORC_MAX_PATH);
    if (FAILED(hr = GetOutputFile(szInputString, szOutputFile, ORC_MAX_PATH, bCreateParentAsNeeded)))
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

HRESULT
Orc::ExpandPath(const WCHAR* szInputString, std::wstring& inputPath, bool checkExists)
{
    inputPath.resize(ORC_MAX_PATH);

    DWORD dwRequiredLen = ExpandEnvironmentStringsW(szInputString, NULL, 0L);
    if (dwRequiredLen > inputPath.size())
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    dwRequiredLen = ExpandEnvironmentStringsW(szInputString, inputPath.data(), inputPath.size());
    if (dwRequiredLen == 0 || dwRequiredLen > inputPath.size())
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (dwRequiredLen > 0)
    {
        inputPath.resize(dwRequiredLen - 1);  // Remove null terminator
    }

    if (!checkExists)
    {
        return S_OK;
    }

    DWORD dwAttr = GetFileAttributesW(inputPath.c_str());

    if (INVALID_FILE_ATTRIBUTES == dwAttr)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
Orc::ExpandFilePath(const WCHAR* szInputString, WCHAR* szInputFile, DWORD cchInputFileLengthInWCHARS, bool exists)
{
    DWORD dwRequiredLen = ExpandEnvironmentStrings(szInputString, NULL, 0L);

    if (dwRequiredLen > cchInputFileLengthInWCHARS)
        return ERROR_INSUFFICIENT_BUFFER;

    ZeroMemory(szInputFile, cchInputFileLengthInWCHARS * sizeof(WCHAR));
    ExpandEnvironmentStrings(szInputString, szInputFile, cchInputFileLengthInWCHARS);

    DWORD dwAttr = GetFileAttributes(szInputFile);

    // Checking if file exists and is not a directory
    if (INVALID_FILE_ATTRIBUTES == dwAttr)
    {
        auto lastError = GetLastError();
        if (exists == false && lastError == ERROR_FILE_NOT_FOUND || lastError == ERROR_PATH_NOT_FOUND)
        {
            return S_OK;
        }

        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (FILE_ATTRIBUTE_DIRECTORY & dwAttr)
        return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);

    return S_OK;
}

HRESULT Orc::ExpandFilePath(const WCHAR* szInputString, std::wstring& strInputFile, bool exists)
{
    HRESULT hr = E_FAIL;

    WCHAR szInputFile[ORC_MAX_PATH];
    ZeroMemory(szInputFile, sizeof(WCHAR) * ORC_MAX_PATH);
    if (FAILED(hr = ExpandFilePath(szInputString, szInputFile, ORC_MAX_PATH, exists)))
        return hr;
    strInputFile.assign(szInputFile);
    return S_OK;
}

HRESULT Orc::ExpandDirectoryPath(const WCHAR* szString, WCHAR* szDirectory, DWORD cchInputDir)
{
    DWORD dwRequiredLen = ExpandEnvironmentStrings(szString, NULL, 0L);

    if (dwRequiredLen > cchInputDir)
        return ERROR_INSUFFICIENT_BUFFER;

    ZeroMemory(szDirectory, cchInputDir * sizeof(WCHAR));
    ExpandEnvironmentStrings(szString, szDirectory, cchInputDir);

    DWORD dwAttr = GetFileAttributes(szDirectory);

    // Checking if file exists and is not a directory
    if (INVALID_FILE_ATTRIBUTES == dwAttr)
        return HRESULT_FROM_WIN32(GetLastError());
    if (FILE_ATTRIBUTE_DIRECTORY & dwAttr)
        return S_OK;

    return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
}

HRESULT Orc::ExpandDirectoryPath(const WCHAR* szInput, std::wstring& inputDir)
{
    HRESULT hr = E_FAIL;

    WCHAR szInputDir[ORC_MAX_PATH];
    ZeroMemory(szInputDir, sizeof(szInputDir));
    hr = ExpandDirectoryPath(szInput, szInputDir, ORC_MAX_PATH);
    if (FAILED(hr))
    {
        return hr;
    }

    inputDir.assign(szInputDir);
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

HRESULT Orc::GetProcessModuleDirectory(WCHAR* szDirectory, DWORD cchDirectoryLengthInWCHARS)
{
    WCHAR szProcessFileName[ORC_MAX_PATH];

    if (!GetModuleFileName(NULL, szProcessFileName, ORC_MAX_PATH))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return GetDirectoryForFile(szProcessFileName, szDirectory, cchDirectoryLengthInWCHARS);
}

HRESULT Orc::GetProcessModuleFileName(WCHAR* szFileName, DWORD cchFileNameLengthInWCHARS)
{
    WCHAR szProcessFileName[ORC_MAX_PATH];

    if (!GetModuleFileName(NULL, szProcessFileName, ORC_MAX_PATH))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return GetFileNameForFile(szProcessFileName, szFileName, cchFileNameLengthInWCHARS);
}

HRESULT Orc::GetProcessModuleFullPath(WCHAR* szFullPath, DWORD cchFullPathLengthInWCHARS)
{
    if (!GetModuleFileName(NULL, szFullPath, cchFullPathLengthInWCHARS))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

HRESULT
Orc::GetDirectoryForFile(const WCHAR* szInputString, WCHAR* szDirectory, DWORD cchDirectoryLengthInWCHARS)
{
    WCHAR path_buffer[ORC_MAX_PATH];
    WCHAR drive[_MAX_DRIVE];
    WCHAR dir[_MAX_DIR];
    WCHAR fname[_MAX_FNAME];
    WCHAR ext[_MAX_EXT];
    errno_t err;

    size_t dwLen = wcslen(szInputString);

    if (dwLen > ORC_MAX_PATH)
        return E_INVALIDARG;
    if (dwLen < 1)
        return E_INVALIDARG;

    wcscpy_s(path_buffer, szInputString);
    if (szInputString[dwLen - 1] == L'\\')
        path_buffer[dwLen - 1] = L'\0';

    err = _wsplitpath_s(path_buffer, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
    if (err != 0)
        return E_INVALIDARG;
    ZeroMemory(path_buffer, ORC_MAX_PATH * sizeof(WCHAR));

    err = _wmakepath_s(szDirectory, cchDirectoryLengthInWCHARS, drive, dir, NULL, NULL);

    return S_OK;
}

HRESULT
Orc::GetFileNameForFile(const WCHAR* szInputString, WCHAR* szFileName, DWORD cchFileNameLengthInWCHARS)
{
    WCHAR path_buffer[ORC_MAX_PATH];
    WCHAR drive[_MAX_DRIVE];
    WCHAR dir[_MAX_DIR];
    WCHAR fname[_MAX_FNAME];
    WCHAR ext[_MAX_EXT];
    errno_t err;

    size_t dwLen = wcslen(szInputString);
    if (dwLen < 1)
        return E_INVALIDARG;

    if (dwLen > ORC_MAX_PATH)
        return E_INVALIDARG;

    wcscpy_s(path_buffer, szInputString);
    if (szInputString[dwLen - 1] == L'\\')
        path_buffer[dwLen - 1] = L'\0';

    err = _wsplitpath_s(path_buffer, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
    if (err != 0)
        return E_INVALIDARG;
    ZeroMemory(path_buffer, ORC_MAX_PATH * sizeof(WCHAR));

    err = _wmakepath_s(szFileName, cchFileNameLengthInWCHARS, NULL, NULL, fname, ext);
    if (err != 0)
        return E_INVALIDARG;
    return S_OK;
}

HRESULT
Orc::GetExtensionForFile(const WCHAR* szInputString, WCHAR* szExtension, DWORD cchExtensionLengthInWCHARS)
{
    WCHAR path_buffer[ORC_MAX_PATH];

    errno_t err;

    size_t dwLen = wcslen(szInputString);
    if (dwLen > ORC_MAX_PATH)
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

HRESULT
Orc::GetBaseNameForFile(const WCHAR* szInputString, WCHAR* szBaseName, DWORD cchBaseNameLengthInWCHARS)
{
    WCHAR path_buffer[ORC_MAX_PATH];

    errno_t err;

    size_t dwLen = wcslen(szInputString);
    if (dwLen > ORC_MAX_PATH)
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

HRESULT Orc::GetFileNameAndDirectoryForFile(
    const WCHAR* szInputString,
    WCHAR* szDirectory,
    DWORD cchDirectoryLengthInWCHARS,
    WCHAR* szFileName,
    DWORD cchFileNameLengthInWCHARS)
{
    WCHAR path_buffer[ORC_MAX_PATH];
    WCHAR drive[_MAX_DRIVE];
    WCHAR dir[_MAX_DIR];
    WCHAR fname[_MAX_FNAME];
    WCHAR ext[_MAX_EXT];
    errno_t err;

    size_t dwLen = wcslen(szInputString);

    if (dwLen > ORC_MAX_PATH)
        return E_INVALIDARG;
    if (dwLen < 1)
        return E_INVALIDARG;

    wcscpy_s(path_buffer, szInputString);
    if (szInputString[dwLen - 1] == L'\\')
        path_buffer[dwLen - 1] = L'\0';

    err = _wsplitpath_s(path_buffer, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
    if (err != 0)
        return E_INVALIDARG;
    ZeroMemory(path_buffer, ORC_MAX_PATH * sizeof(WCHAR));

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
            WCHAR path_buffer[ORC_MAX_PATH];
            WCHAR drive[_MAX_DRIVE];
            WCHAR dir[_MAX_DIR];
            WCHAR fname[_MAX_FNAME];
            WCHAR ext[_MAX_EXT];
            errno_t err;

            size_t dwLen = wcslen(szOutputCab);
            if (dwLen > ORC_MAX_PATH)
                return E_INVALIDARG;
            if (dwLen < 1)
                return E_INVALIDARG;

            if (szOutputCab[dwLen - 1] == L'\\')
                szOutputCab[dwLen - 1] = L'\0';

            err = _wsplitpath_s(szOutputCab, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
            if (err != 0)
                return E_INVALIDARG;

            err = _wmakepath_s(path_buffer, ORC_MAX_PATH, drive, dir, NULL, NULL);
            if (err != 0)
                return E_INVALIDARG;

            return GetOutputDir_Internal(path_buffer, ORC_MAX_PATH, true, MAX_CREATEDIR_RECURSIONS);
        }
        return S_OK;
    }
    if (FILE_ATTRIBUTE_DIRECTORY & dwAttr)
        return HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    return S_OK;
}

HRESULT Orc::GetOutputCab(const WCHAR* szInputString, std::wstring& strOutputCab, bool bCreateParentAsNeeded)
{
    HRESULT hr = E_FAIL;

    WCHAR szOutputCab[ORC_MAX_PATH];
    ZeroMemory(szOutputCab, sizeof(WCHAR) * ORC_MAX_PATH);
    if (FAILED(hr = GetOutputCab(szInputString, szOutputCab, ORC_MAX_PATH, bCreateParentAsNeeded)))
        return hr;
    strOutputCab.assign(szOutputCab);
    return S_OK;
}

HRESULT Orc::GetFileSizeFromArg(const WCHAR* pSize, LARGE_INTEGER& size, int radix)
{
    size.HighPart = 0L;
    size.LowPart = 0L;

    if (pSize == nullptr)
    {
        Log::Debug(L"GetFileSizeFromArg: invalid nullptr");
        return E_POINTER;
    }

    // Build a view to trim and parse suffixes (KB/MB/GB or single-letter K/M/G/B)
    std::wstring_view sv(pSize);
    // Trim trailing spaces
    while (!sv.empty() && iswspace(static_cast<unsigned short>(sv.back())))
        sv.remove_suffix(1);

    if (sv.empty())
        return E_INVALIDARG;

    unsigned long long multiplier = 1ULL;
    size_t removeChars = 0;

    if (sv.size() >= 2 && (sv.back() == L'B' || sv.back() == L'b'))
    {
        wchar_t prev = sv[sv.size() - 2];
        if (prev == L'K' || prev == L'k')
        {
            multiplier = 1024ULL;
            removeChars = 2;
        }
        else if (prev == L'M' || prev == L'm')
        {
            multiplier = 1024ULL * 1024ULL;
            removeChars = 2;
        }
        else if (prev == L'G' || prev == L'g')
        {
            multiplier = 1024ULL * 1024ULL * 1024ULL;
            removeChars = 2;
        }
        else
        {
            // Just a trailing B/b means bytes
            multiplier = 1ULL;
            removeChars = 1;
        }
    }
    else if (!sv.empty())
    {
        wchar_t last = sv.back();
        if (last == L'K' || last == L'k')
        {
            multiplier = 1024ULL;
            removeChars = 1;
        }
        else if (last == L'M' || last == L'm')
        {
            multiplier = 1024ULL * 1024ULL;
            removeChars = 1;
        }
        else if (last == L'G' || last == L'g')
        {
            multiplier = 1024ULL * 1024ULL * 1024ULL;
            removeChars = 1;
        }
    }

    std::wstring numberPart(sv.substr(0, sv.size() - removeChars));

    // Trim spaces on numberPart end
    while (!numberPart.empty() && iswspace(static_cast<unsigned short>(numberPart.back())))
        numberPart.pop_back();

    if (numberPart.empty())
        return E_INVALIDARG;

    wchar_t* endptr = nullptr;
    _set_errno(0);
    unsigned long long base = std::wcstoull(numberPart.c_str(), &endptr, radix);

    int errsv = 0;
    _get_errno(&errsv);

    if (endptr == numberPart.c_str())
    {
        Log::Debug(L"GetFileSizeFromArg: no digits found (input: {})", numberPart);
        return E_INVALIDARG;
    }

    if (errsv == ERANGE)
    {
        Log::Debug(L"GetFileSizeFromArg: overflow in conversion (input: {})", numberPart);
        return E_INVALIDARG;
    }

    // Skip any trailing spaces
    while (endptr && *endptr && iswspace(static_cast<unsigned short>(*endptr)))
        ++endptr;

    if (endptr && *endptr != L'\0')
    {
        Log::Debug(L"GetFileSizeFromArg: trailing non-numeric characters (input: {})", numberPart);
        return E_INVALIDARG;
    }

    // Check multiplication overflow and fit in signed64-bit
    if (base > (std::numeric_limits<unsigned long long>::max)() / multiplier)
    {
        Log::Debug(L"GetFileSizeFromArg: multiplication overflow");
        return E_INVALIDARG;
    }

    unsigned long long total = base * multiplier;
    if (total > static_cast<unsigned long long>((std::numeric_limits<LONGLONG>::max)()))
    {
        Log::Debug(L"GetFileSizeFromArg: exceeds64-bit signed range");
        return E_INVALIDARG;
    }

    size.QuadPart = static_cast<LONGLONG>(total);
    return S_OK;
}

HRESULT Orc::GetPercentageFromArg(const WCHAR* pStr, DWORD& value)
{
    if (pStr == nullptr)
    {
        Log::Debug(L"GetPercentageFromArg: invalid nullptr");
        return E_POINTER;
    }

    // Trim leading/trailing spaces
    std::wstring_view sv(pStr);
    while (!sv.empty() && iswspace(static_cast<unsigned short>(sv.front())))
        sv.remove_prefix(1);

    while (!sv.empty() && iswspace(static_cast<unsigned short>(sv.back())))
        sv.remove_suffix(1);

    if (!sv.empty() && sv.back() == L'%')
    {
        sv.remove_suffix(1);
    }

    std::wstring tmp(sv);
    if (tmp.empty())
        return E_INVALIDARG;

    wchar_t* end = nullptr;
    _set_errno(0);
    unsigned long parsed = std::wcstoul(tmp.c_str(), &end, 10);

    int errsv = 0;
    _get_errno(&errsv);

    if (end == tmp.c_str())
    {
        Log::Debug(L"GetPercentageFromArg: no digits found (input: {})", tmp);
        return E_INVALIDARG;
    }

    if (errsv == ERANGE)
    {
        Log::Debug(L"GetPercentageFromArg: overflow in conversion (input: {})", tmp);
        return E_INVALIDARG;
    }

    while (end && *end && iswspace(static_cast<unsigned short>(*end)))
        ++end;

    if (end && *end != L'\0')
        return E_INVALIDARG;

    if (parsed > 100)
        return E_INVALIDARG;

    // Preserve original semantics:0 is considered invalid
    if (parsed == 0)
    {
        Log::Debug(L"GetPercentageFromArg: failed because parsed==0");
        return E_INVALIDARG;
    }

    try
    {
        value = ConvertTo<DWORD>(parsed);
    }
    catch (const std::overflow_error&)
    {
        Log::Debug(L"GetPercentageFromArg: failed because overflow error (input: {})", parsed);
        return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT Orc::GetIntegerFromArg(const WCHAR* pSize, LARGE_INTEGER& size, int radix)
{
    size.HighPart = 0L;
    size.LowPart = 0L;

    if (pSize == nullptr)
    {
        Log::Debug(L"GetIntegerFromArg (LARGE_INTEGER): invalid nullptr");
        return E_POINTER;
    }

    _set_errno(0);
    wchar_t* end = nullptr;
    long long parsed = std::wcstoll(pSize, &end, radix);

    int errsv = 0;
    _get_errno(&errsv);

    if (end == pSize)
    {
        Log::Debug(L"GetIntegerFromArg (LARGE_INTEGER): no digits found (input: {})", pSize);
        return E_INVALIDARG;
    }

    if (errsv == ERANGE)
    {
        Log::Debug(L"GetIntegerFromArg (LARGE_INTEGER): overflow in conversion (input: {})", pSize);
        return E_INVALIDARG;
    }

    while (end && *end && iswspace(static_cast<unsigned short>(*end)))
        ++end;

    if (end && *end != L'\0')
    {
        Log::Debug(L"GetIntegerFromArg (LARGE_INTEGER): trailing non-numeric (input: {})", pSize);
        return E_INVALIDARG;
    }

    size.QuadPart = parsed;
    return S_OK;
}

HRESULT Orc::GetIntegerFromArg(const CHAR* pSize, LARGE_INTEGER& size, int radix)
{
    size.HighPart = 0L;
    size.LowPart = 0L;

    if (pSize == nullptr)
    {
        Log::Debug(L"GetIntegerFromArg (LARGE_INTEGER): invalid nullptr");
        return E_POINTER;
    }

    char* end = nullptr;
    _set_errno(0);
    long long parsed = std::strtoll(pSize, &end, radix);

    int errsv = 0;
    _get_errno(&errsv);

    if (end == pSize)
    {
        Log::Debug("GetIntegerFromArg (LARGE_INTEGER): no digits found (input: {})", pSize);
        return E_INVALIDARG;
    }

    if (errsv == ERANGE)
    {
        Log::Debug("GetIntegerFromArg (LARGE_INTEGER): overflow in conversion (input: {})", pSize);
        return E_INVALIDARG;
    }

    while (end && *end && isspace(*end))
        ++end;

    if (end && *end != '\0')
    {
        Log::Debug("GetIntegerFromArg (LARGE_INTEGER): trailing non-numeric (input: {})", pSize);
        return E_INVALIDARG;
    }

    size.QuadPart = parsed;
    return S_OK;
}

HRESULT Orc::GetIntegerFromArg(const WCHAR* pSize, ULARGE_INTEGER& size, int radix)
{
    size.HighPart = 0L;
    size.LowPart = 0L;

    if (pSize == nullptr)
    {
        Log::Debug(L"GetIntegerFromArg (ULARGE_INTEGER): invalid nullptr");
        return E_POINTER;
    }

    wchar_t* end = nullptr;
    _set_errno(0);
    unsigned long long parsed = std::wcstoull(pSize, &end, radix);

    int errsv = 0;
    _get_errno(&errsv);

    if (end == pSize)
    {
        Log::Debug(L"GetIntegerFromArg (ULARGE_INTEGER): no digits found (input: {})", pSize);
        return E_INVALIDARG;
    }

    if (errsv == ERANGE)
    {
        Log::Debug(L"GetIntegerFromArg (ULARGE_INTEGER): overflow in conversion (input: {})", pSize);
        return E_INVALIDARG;
    }

    while (end && *end && iswspace(static_cast<unsigned short>(*end)))
        ++end;

    if (end && *end != L'\0')
    {
        Log::Debug(L"GetIntegerFromArg (ULARGE_INTEGER): trailing non-numeric (input: {})", pSize);
        return E_INVALIDARG;
    }

    size.QuadPart = parsed;
    return S_OK;
}

HRESULT Orc::GetIntegerFromArg(const CHAR* pSize, ULARGE_INTEGER& size, int radix)
{
    size.HighPart = 0L;
    size.LowPart = 0L;

    if (pSize == nullptr)
    {
        Log::Debug(L"GetIntegerFromArg (ULARGE_INTEGER): invalid nullptr");
        return E_POINTER;
    }

    char* end = nullptr;
    _set_errno(0);
    unsigned long long parsed = std::strtoull(pSize, &end, radix);

    int errsv = 0;
    _get_errno(&errsv);

    if (end == pSize)
    {
        Log::Debug("GetIntegerFromArg (ULARGE_INTEGER): no digits found (input: {})", pSize);
        return E_INVALIDARG;
    }

    if (errsv == ERANGE)
    {
        Log::Debug("GetIntegerFromArg (ULARGE_INTEGER): overflow in conversion (input: {})", pSize);
        return E_INVALIDARG;
    }

    while (end && *end && isspace(static_cast<unsigned char>(*end)))
        ++end;

    if (end && *end != '\0')
    {
        Log::Debug("GetIntegerFromArg (ULARGE_INTEGER): trailing non-numeric (input: {})", pSize);
        return E_INVALIDARG;
    }

    size.QuadPart = parsed;
    return S_OK;
}

HRESULT Orc::GetIntegerFromArg(const WCHAR* pStr, size_t& size, int radix)
{
    if (pStr == nullptr)
    {
        Log::Debug(L"GetIntegerFromArg (size_t): invalid nullptr");
        return E_POINTER;
    }

    wchar_t* end = nullptr;
    _set_errno(0);
    unsigned long long parsed = std::wcstoull(pStr, &end, radix);

    int errsv = 0;
    _get_errno(&errsv);

    if (end == pStr)
    {
        Log::Debug(L"GetIntegerFromArg (size_t): no digits found (input: {})", pStr);
        return E_INVALIDARG;
    }

    if (errsv == ERANGE)
    {
        Log::Debug(L"GetIntegerFromArg (size_t): overflow in conversion (input: {})", pStr);
        return E_INVALIDARG;
    }

    while (end && *end && iswspace(static_cast<unsigned short>(*end)))
        ++end;

    if (end && *end != L'\0')
        return E_INVALIDARG;

    if (parsed == 0)
    {
        Log::Debug(L"GetIntegerFromArg (size_t): failed because parsed==0");
        return E_INVALIDARG;
    }

    try
    {
        if (parsed > static_cast<unsigned long long>((std::numeric_limits<size_t>::max)()))
            throw std::overflow_error("size_t overflow");
        size = ConvertTo<size_t>(parsed);
    }
    catch (const std::overflow_error&)
    {
        Log::Debug(L"GetIntegerFromArg (size_t): failed because overflow error (input: {})", parsed);
        return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT Orc::GetIntegerFromArg(const WCHAR* pStr, DWORD& value, int radix)
{
    if (pStr == nullptr)
    {
        Log::Debug(L"GetIntegerFromArg (DWORD): invalid nullptr");
        return E_POINTER;
    }

    wchar_t* end = nullptr;
    _set_errno(0);
    unsigned long parsed = std::wcstoul(pStr, &end, radix);

    int errsv = 0;
    _get_errno(&errsv);

    if (end == pStr)
    {
        Log::Debug(L"GetIntegerFromArg (DWORD): no digits found (input: {})", pStr);
        return E_INVALIDARG;
    }

    if (errsv == ERANGE)
    {
        Log::Debug(L"GetIntegerFromArg (DWORD): overflow in conversion (input: {})", pStr);
        return E_INVALIDARG;
    }

    while (end && *end && iswspace(static_cast<unsigned short>(*end)))
        ++end;

    if (end && *end != L'\0')
        return E_INVALIDARG;

    if (parsed == 0)
    {
        Log::Debug(L"GetIntegerFromArg (DWORD): failed because parsed==0");
        return E_INVALIDARG;
    }

    try
    {
        value = ConvertTo<DWORD>(parsed);
    }
    catch (const std::overflow_error&)
    {
        Log::Debug(L"GetIntegerFromArg (DWORD): failed because overflow error (input: {})", parsed);
        return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT Orc::GetIntegerFromArg(const CHAR* pStr, DWORD& value, int radix)
{
    if (pStr == nullptr)
    {
        Log::Debug(L"GetIntegerFromArg (DWORD): invalid nullptr");
        return E_POINTER;
    }

    char* end = nullptr;
    _set_errno(0);
    unsigned long parsed = std::strtoul(pStr, &end, radix);

    int errsv = 0;
    _get_errno(&errsv);

    if (end == pStr)
    {
        Log::Debug("GetIntegerFromArg (DWORD): no digits found (input: {})", pStr);
        return E_INVALIDARG;
    }

    if (errsv == ERANGE)
    {
        Log::Debug("GetIntegerFromArg (DWORD): overflow in conversion (input: {})", pStr);
        return E_INVALIDARG;
    }

    while (end && *end && isspace(static_cast<unsigned char>(*end)))
        ++end;

    if (end && *end != '\0')
        return E_INVALIDARG;

    if (parsed == 0)
    {
        Log::Debug("GetIntegerFromArg (DWORD): failed because parsed==0");
        return E_INVALIDARG;
    }

    try
    {
        value = ConvertTo<DWORD>(parsed);
    }
    catch (const std::overflow_error&)
    {
        Log::Debug(L"GetIntegerFromArg (DWORD): failed because overflow error (input: {})", parsed);
        return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT Orc::GetIntegerFromHexaString(const WCHAR* pszStr, DWORD& result)
{
    return GetIntegerFromArg(pszStr, result, 16);
}

HRESULT Orc::GetIntegerFromHexaString(const CHAR* pszStr, DWORD& result)
{
    return GetIntegerFromArg(pszStr, result, 16);
}

HRESULT Orc::GetIntegerFromHexaString(const WCHAR* pszStr, LARGE_INTEGER& result)
{
    return GetIntegerFromArg(pszStr, result, 16);
}

HRESULT Orc::GetIntegerFromHexaString(const CHAR* pszStr, LARGE_INTEGER& result)
{
    return GetIntegerFromArg(pszStr, result, 16);
}

HRESULT
Orc::GetBytesFromHexaString(
    const WCHAR* pszStr,
    DWORD dwStrLen,
    BYTE* pBytes,
    DWORD dwBytesLen,
    DWORD* pdwBytesLen,
    bool bMustBe0xPrefixed)
{
    if (pszStr == NULL || pBytes == NULL)
        return E_POINTER;
    if (dwStrLen % 2)
        return E_INVALIDARG;

    if (dwStrLen == 0)
    {
        if (pdwBytesLen)
            *pdwBytesLen = 0L;

        ZeroMemory(pBytes, dwBytesLen);
        return S_OK;
    }

    DWORD dwStartAt = 0L;
    if (pszStr[0] == L'0' && (pszStr[1] == L'x' || pszStr[1] == L'X'))
    {
        dwStartAt = 2;
        if (dwBytesLen < (dwStrLen - 2) / 2)
            return E_INVALIDARG;
    }
    else
    {
        if (bMustBe0xPrefixed)
            return E_INVALIDARG;
        if (dwBytesLen < dwStrLen / 2)
            return E_INVALIDARG;
    }

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
Orc::GetBytesFromHexaString(
    const CHAR* pszStr,
    DWORD dwStrLen,
    BYTE* pBytes,
    DWORD dwBytesLen,
    DWORD* pdwBytesLen,
    bool bMustBe0xPrefixed)
{
    if (pszStr == NULL || pBytes == NULL)
        return E_POINTER;
    if (dwStrLen % 2)
        return E_INVALIDARG;

    if (dwStrLen == 0)
    {
        if (pdwBytesLen)
            *pdwBytesLen = 0L;
        ZeroMemory(pBytes, dwBytesLen);
        return S_OK;
    }

    DWORD dwStartAt = 0L;
    if (pszStr[0] == '0' && (pszStr[1] == 'x' || pszStr[1] == 'X'))
    {
        dwStartAt = 2;
        if (dwBytesLen < (dwStrLen - 2) / 2)
            return E_INVALIDARG;
    }
    else
    {
        if (bMustBe0xPrefixed)
            return E_INVALIDARG;
        if (dwBytesLen < dwStrLen / 2)
            return E_INVALIDARG;
    }

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

HRESULT Orc::GetBytesFromHexaString(const WCHAR* pszStr, DWORD dwStrLen, CBinaryBuffer& buffer, bool bMustBe0xPrefixed)
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
    {
        if (bMustBe0xPrefixed)
            return E_INVALIDARG;

        buffer.SetCount(dwStrLen / 2);
    }

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

HRESULT Orc::GetBytesFromHexaString(const CHAR* pszStr, DWORD dwStrLen, CBinaryBuffer& buffer, bool bMustBe0xPrefixed)
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
    {
        if (bMustBe0xPrefixed)
            return E_INVALIDARG;

        buffer.SetCount(dwStrLen / 2);
    }

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
        _set_errno(0);
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
