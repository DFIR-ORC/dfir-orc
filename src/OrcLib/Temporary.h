//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include <string>

#include "OrcLib.h"

#pragma managed(push, off)

namespace Orc {

HRESULT UtilGetTempDirPath(__out_ecount(dwPathSize) PWSTR pwzTempDir, __in DWORD dwPathSize);

HRESULT UtilGetTempFile(
    __out_opt HANDLE* phFile,
    __in_opt PCWSTR pwszDir,
    __in PCWSTR pwszExt,
    __out_ecount(cchFileName) PWSTR pwszFilePath,
    __in DWORD cchFileName,
    __in_opt LPSECURITY_ATTRIBUTES psa,
    __in_opt DWORD dwShareMode = FILE_SHARE_READ,
    __in_opt DWORD dwFlags = 0);

HRESULT UtilGetTempFile(
    __out_opt HANDLE* phFile,
    __in_opt PCWSTR pwszDir,
    __in PCWSTR pwszExt,
    std::wstring& strFilePath,
    __in_opt LPSECURITY_ATTRIBUTES psa,
    __in_opt DWORD dwShareMode = FILE_SHARE_READ,
    __in_opt DWORD dwFlags = 0);

HRESULT UtilGetUniquePath(__in PCWSTR pwzDir, __in PCWSTR pwzDesiredName, __out std::wstring& pstrDestPath);
HRESULT UtilGetUniquePath(
    __in PCWSTR pwzDir,
    __in PCWSTR pwzDesiredName,
    __out std::wstring& pstrDestPath,
    __out HANDLE& hFile,
    __in DWORD dwFlags = FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
    LPCWSTR szSDDL = NULL);

HRESULT UtilGetPath(__in PCWSTR pwzDir, __in PCWSTR pwzDesiredName, __out std::wstring& pstrDestPath);

BOOL UtilPathIsDirectory(__in PCWSTR pwszPath);

HRESULT UtilDeleteTemporaryFile(LPCWSTR pszPath);
HRESULT UtilDeleteTemporaryFile(const std::filesystem::path& path);

HRESULT UtilDeleteTemporaryDirectory(const std::filesystem::path& path);

static constexpr auto DELETION_RETRIES = 50;

HRESULT UtilDeleteTemporaryFile(__in LPCWSTR pszPath, DWORD dwMaxRetries);

}  // namespace Orc

#pragma managed(pop)
