//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
#pragma once

#include <windows.h>
#include <bcrypt.h>

#include <system_error>

// CNG entry points resolved at run time, never imported.
//
// bcrypt.dll does not exist on Windows XP. A static import would put it in the x86-xp binary's import
// table and XP would then refuse to launch DFIR-ORC_x86-xp.exe AT ALL, for every collection, whether or
// not it uses a scheme that needs CNG. Resolving lazily keeps that binary loadable and turns "no CNG on
// this host" into an error_code at the first signing call. Same pattern as
// Filesystem/Ntfs/Compression/Engine/Nt/NtApi.cpp and CpuInfo.cpp.
//
// DO NOT add bcrypt to target_link_libraries or to a #pragma comment(lib, ...).

namespace Orc {
namespace BCryptApi {

NTSTATUS OpenAlgorithmProvider(BCRYPT_ALG_HANDLE* phAlgorithm, LPCWSTR pszAlgId, ULONG dwFlags, std::error_code& ec);
NTSTATUS CloseAlgorithmProvider(BCRYPT_ALG_HANDLE hAlgorithm, std::error_code& ec);
NTSTATUS CreateHash(
    BCRYPT_ALG_HANDLE hAlgorithm,
    BCRYPT_HASH_HANDLE* phHash,
    PUCHAR pbSecret,
    ULONG cbSecret,
    std::error_code& ec);
NTSTATUS HashData(BCRYPT_HASH_HANDLE hHash, PUCHAR pbInput, ULONG cbInput, std::error_code& ec);
NTSTATUS FinishHash(BCRYPT_HASH_HANDLE hHash, PUCHAR pbOutput, ULONG cbOutput, std::error_code& ec);
NTSTATUS DestroyHash(BCRYPT_HASH_HANDLE hHash, std::error_code& ec);

}  // namespace BCryptApi
}  // namespace Orc
