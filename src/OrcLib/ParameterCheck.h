//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "OutputSpec.h"

#pragma managed(push, off)

namespace Orc {

HRESULT ExpandFilePath(const WCHAR* szInputString, WCHAR* szInputFile, DWORD cchInputFileLengthInWCHARS);
HRESULT ExpandFilePath(const WCHAR* szInputString, std::wstring& strInputFile);

HRESULT
ExpandDirectoryPath(const WCHAR* szInputString, WCHAR* szInputFile, DWORD cchInputFileLengthInWCHARS);
HRESULT ExpandDirectoryPath(const WCHAR* szInputString, std::wstring& strInputFile);

HRESULT VerifyFileExists(const WCHAR* szInputFile);
HRESULT VerifyDirectoryExists(const WCHAR* szInputDir);
HRESULT VerifyFileIsBinary(const WCHAR* szInputFile);

HRESULT IsPathComplete(const WCHAR* szInputFile);
HRESULT IsFileName(const WCHAR* szInputFile);

HRESULT GetProcessModuleDirectory(WCHAR* szDirectory, DWORD cchDirectoryLengthInWCHARS);
HRESULT GetProcessModuleFileName(WCHAR* szDirectory, DWORD cchDirectoryLengthInWCHARS);
HRESULT GetProcessModuleFullPath(WCHAR* szDirectory, DWORD cchDirectoryLengthInWCHARS);

HRESULT
GetDirectoryForFile(const WCHAR* szInputString, WCHAR* szDirectory, DWORD cchDirectoryLengthInWCHARS);
HRESULT GetFileNameForFile(const WCHAR* szInputString, WCHAR* szFileName, DWORD cchFileNameLengthInWCHARS);
HRESULT GetExtensionForFile(const WCHAR* szInputString, WCHAR* szExtension, DWORD cchFileNameLengthInWCHARS);
HRESULT GetBaseNameForFile(const WCHAR* szInputString, WCHAR* szBaseName, DWORD cchBaseNameLengthInWCHARS);

HRESULT GetFileNameAndDirectoryForFile(
    const WCHAR* szInputString,
    WCHAR* szDirectory,
    DWORD cchDirectoryLengthInWCHARS,
    WCHAR* szFileName,
    DWORD cchFileNameLengthInWCHARS);

HRESULT GetOutputFile(
    const WCHAR* szInputString,
    WCHAR* szOutputFile,
    DWORD cchOutputFileLengthInWCHARS,
    bool bCreateParentAsNeeded = false);
HRESULT
GetOutputFile(const WCHAR* szInputString, std::wstring& strOutputFile, bool bCreateParentAsNeeded = false);

HRESULT GetOutputDir(
    const WCHAR* szInputString,
    WCHAR* szOutputDir,
    DWORD cchOutputFileLengthInWCHARS,
    bool bAllowRecursiveFolderCreation = false);
HRESULT
GetOutputDir(const WCHAR* szInputString, std::wstring& strOutputDir, bool bAllowRecursiveFolderCreation = false);

HRESULT GetOutputCab(
    const WCHAR* szInputString,
    WCHAR* szOutputCab,
    DWORD cchOutputCabLengthInWCHARS,
    bool bAllowRecursiveFolderCreation = false);
HRESULT
GetOutputCab(const WCHAR* szInputString, std::wstring& strOutputCab, bool bCreateParentAsNeeded = false);

HRESULT GetFileSizeFromArg(const WCHAR* pSize, LARGE_INTEGER& size);
HRESULT GetPercentageFromArg(const WCHAR* pStr, DWORD& value);

HRESULT GetIntegerFromArg(const WCHAR* pStr, LARGE_INTEGER& size);
HRESULT GetIntegerFromArg(const WCHAR* pStr, size_t& size);
HRESULT GetIntegerFromArg(const WCHAR* pStr, DWORD& value);

HRESULT GetIntegerFromHexaString(const WCHAR* pszStr, DWORD& result);
HRESULT GetIntegerFromHexaString(const WCHAR* pszStr, LARGE_INTEGER& result);
HRESULT GetIntegerFromHexaString(const CHAR* pszStr, DWORD& result);
HRESULT GetIntegerFromHexaString(const CHAR* pszStr, LARGE_INTEGER& result);

HRESULT GetBytesFromHexaString(
    const WCHAR* pszStr,
    DWORD dwStrLen,
    BYTE* pBytes,
    DWORD dwMaxBytesLen,
    DWORD* pdwBytesLen = nullptr);
HRESULT GetBytesFromHexaString(const WCHAR* pszStr, DWORD dwStrLen, CBinaryBuffer& buffer);
HRESULT GetBytesFromHexaString(
    const CHAR* pszStr,
    DWORD dwStrLen,
    BYTE* pBytes,
    DWORD dwMaxBytesLen,
    DWORD* pdwBytesLen = nullptr);
HRESULT GetBytesFromHexaString(const CHAR* pszStr, DWORD dwStrLen, CBinaryBuffer& buffer);

HRESULT GetDateFromString(const WCHAR* szFormat, const WCHAR* szString, FILETIME& result);

}  // namespace Orc

#pragma managed(pop)
