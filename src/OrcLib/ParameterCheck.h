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

ORCLIB_API HRESULT ExpandFilePath(const WCHAR* szInputString, WCHAR* szInputFile, DWORD cchInputFileLengthInWCHARS);
ORCLIB_API HRESULT ExpandFilePath(const WCHAR* szInputString, std::wstring& strInputFile);

ORCLIB_API HRESULT ExpandDirectoryPath(const WCHAR* szInputString, WCHAR* szInputFile, DWORD cchInputFileLengthInWCHARS);
ORCLIB_API HRESULT ExpandDirectoryPath(const WCHAR* szInputString, std::wstring& strInputFile);

ORCLIB_API HRESULT VerifyFileExists(const WCHAR* szInputFile);
ORCLIB_API HRESULT VerifyDirectoryExists(const WCHAR* szInputDir);
ORCLIB_API HRESULT VerifyFileIsBinary(const WCHAR* szInputFile);

ORCLIB_API HRESULT IsPathComplete(const WCHAR* szInputFile);
ORCLIB_API HRESULT IsFileName(const WCHAR* szInputFile);

ORCLIB_API HRESULT GetProcessModuleDirectory(WCHAR* szDirectory, DWORD cchDirectoryLengthInWCHARS);
ORCLIB_API HRESULT GetProcessModuleFileName(WCHAR* szDirectory, DWORD cchDirectoryLengthInWCHARS);
ORCLIB_API HRESULT GetProcessModuleFullPath(WCHAR* szDirectory, DWORD cchDirectoryLengthInWCHARS);

ORCLIB_API HRESULT
GetDirectoryForFile(const WCHAR* szInputString, WCHAR* szDirectory, DWORD cchDirectoryLengthInWCHARS);
ORCLIB_API HRESULT GetFileNameForFile(const WCHAR* szInputString, WCHAR* szFileName, DWORD cchFileNameLengthInWCHARS);
ORCLIB_API HRESULT GetExtensionForFile(const WCHAR* szInputString, WCHAR* szExtension, DWORD cchFileNameLengthInWCHARS);
ORCLIB_API HRESULT GetBaseNameForFile(const WCHAR* szInputString, WCHAR* szBaseName, DWORD cchBaseNameLengthInWCHARS);

ORCLIB_API HRESULT GetFileNameAndDirectoryForFile(
    const WCHAR* szInputString,
    WCHAR* szDirectory,
    DWORD cchDirectoryLengthInWCHARS,
    WCHAR* szFileName,
    DWORD cchFileNameLengthInWCHARS);

ORCLIB_API HRESULT GetOutputFile(
    const WCHAR* szInputString,
    WCHAR* szOutputFile,
    DWORD cchOutputFileLengthInWCHARS,
    bool bCreateParentAsNeeded = false);
ORCLIB_API HRESULT
GetOutputFile(const WCHAR* szInputString, std::wstring& strOutputFile, bool bCreateParentAsNeeded = false);

ORCLIB_API HRESULT GetOutputDir(
    const WCHAR* szInputString,
    WCHAR* szOutputDir,
    DWORD cchOutputFileLengthInWCHARS,
    bool bAllowRecursiveFolderCreation = false);
ORCLIB_API HRESULT
GetOutputDir(const WCHAR* szInputString, std::wstring& strOutputDir, bool bAllowRecursiveFolderCreation = false);

ORCLIB_API HRESULT GetOutputCab(
    const WCHAR* szInputString,
    WCHAR* szOutputCab,
    DWORD cchOutputCabLengthInWCHARS,
    bool bAllowRecursiveFolderCreation = false);
ORCLIB_API HRESULT
GetOutputCab(const WCHAR* szInputString, std::wstring& strOutputCab, bool bCreateParentAsNeeded = false);

ORCLIB_API HRESULT GetFileSizeFromArg(const WCHAR* pSize, LARGE_INTEGER& size);
ORCLIB_API HRESULT GetPercentageFromArg(const WCHAR* pStr, DWORD& value);

ORCLIB_API HRESULT GetIntegerFromArg(const WCHAR* pStr, LARGE_INTEGER& size);
ORCLIB_API HRESULT GetIntegerFromArg(const WCHAR* pStr, size_t& size);
ORCLIB_API HRESULT GetIntegerFromArg(const WCHAR* pStr, DWORD& value);

ORCLIB_API HRESULT GetIntegerFromHexaString(const WCHAR* pszStr, DWORD& result);
ORCLIB_API HRESULT GetIntegerFromHexaString(const WCHAR* pszStr, LARGE_INTEGER& result);
ORCLIB_API HRESULT GetIntegerFromHexaString(const CHAR* pszStr, DWORD& result);
ORCLIB_API HRESULT GetIntegerFromHexaString(const CHAR* pszStr, LARGE_INTEGER& result);

ORCLIB_API HRESULT GetBytesFromHexaString(
    const WCHAR* pszStr,
    DWORD dwStrLen,
    BYTE* pBytes,
    DWORD dwMaxBytesLen,
    DWORD* pdwBytesLen = nullptr);
ORCLIB_API HRESULT GetBytesFromHexaString(const WCHAR* pszStr, DWORD dwStrLen, CBinaryBuffer& buffer);
ORCLIB_API HRESULT GetBytesFromHexaString(
    const CHAR* pszStr,
    DWORD dwStrLen,
    BYTE* pBytes,
    DWORD dwMaxBytesLen,
    DWORD* pdwBytesLen = nullptr);
ORCLIB_API HRESULT GetBytesFromHexaString(const CHAR* pszStr, DWORD dwStrLen, CBinaryBuffer& buffer);

ORCLIB_API HRESULT GetDateFromString(const WCHAR* szFormat, const WCHAR* szString, FILETIME& result);

}  // namespace Orc

#pragma managed(pop)
