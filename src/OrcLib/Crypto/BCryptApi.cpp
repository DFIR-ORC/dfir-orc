//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
#include "stdafx.h"

#include "BCryptApi.h"

#include "Log/Log.h"

#include <string>

namespace {

// Loaded from System32 by absolute path. This tool runs on possibly-compromised hosts, where a bare
// LoadLibraryW(L"bcrypt.dll") is a DLL search-order hijack. LOAD_LIBRARY_SEARCH_SYSTEM32 needs
// KB2533623 on older systems, so the absolute path is the fallback that works everywhere.
//
// The flag is spelled out because the XP toolchain's SDK headers predate it, as in Robustness.cpp.
const DWORD kLOAD_LIBRARY_SEARCH_SYSTEM32 = 0x00000800;

HMODULE LoadBCrypt(std::error_code& ec)
{
    static HMODULE module = NULL;

    if (module != NULL)
    {
        return module;
    }

    module = LoadLibraryExW(L"bcrypt.dll", NULL, kLOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module != NULL)
    {
        return module;
    }

    wchar_t system32[MAX_PATH] = {0};
    const UINT length = GetSystemDirectoryW(system32, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        ec.assign(::GetLastError(), std::system_category());
        Orc::Log::Debug(L"BCryptApi: GetSystemDirectory failed [{}]", ec);
        return NULL;
    }

    const std::wstring path = std::wstring(system32, length) + L"\\bcrypt.dll";
    module = LoadLibraryExW(path.c_str(), NULL, 0);
    if (module == NULL)
    {
        ec.assign(::GetLastError(), std::system_category());
        Orc::Log::Debug(L"BCryptApi: bcrypt.dll is unavailable on this host [{}]", ec);
        return NULL;
    }

    return module;
}

template <typename FnT>
FnT Resolve(const char* name, FnT& cached, std::error_code& ec)
{
    if (cached != nullptr)
    {
        return cached;
    }

    HMODULE dll = LoadBCrypt(ec);
    if (ec)
    {
        return nullptr;
    }

    cached = reinterpret_cast<FnT>(::GetProcAddress(dll, name));
    if (cached == nullptr)
    {
        ec.assign(::GetLastError(), std::system_category());
        Orc::Log::Debug("BCryptApi: '{}' is missing from bcrypt.dll [{}]", name, ec);
    }

    return cached;
}

}  // namespace

namespace Orc {
namespace BCryptApi {

NTSTATUS OpenAlgorithmProvider(BCRYPT_ALG_HANDLE* phAlgorithm, LPCWSTR pszAlgId, ULONG dwFlags, std::error_code& ec)
{
    using Fn = NTSTATUS(WINAPI*)(BCRYPT_ALG_HANDLE*, LPCWSTR, LPCWSTR, ULONG);
    static Fn fn = nullptr;

    if (!Resolve("BCryptOpenAlgorithmProvider", fn, ec))
    {
        return STATUS_UNSUCCESSFUL;
    }

    return fn(phAlgorithm, pszAlgId, NULL, dwFlags);
}

NTSTATUS CloseAlgorithmProvider(BCRYPT_ALG_HANDLE hAlgorithm, std::error_code& ec)
{
    using Fn = NTSTATUS(WINAPI*)(BCRYPT_ALG_HANDLE, ULONG);
    static Fn fn = nullptr;

    if (!Resolve("BCryptCloseAlgorithmProvider", fn, ec))
    {
        return STATUS_UNSUCCESSFUL;
    }

    return fn(hAlgorithm, 0);
}

NTSTATUS
CreateHash(
    BCRYPT_ALG_HANDLE hAlgorithm,
    BCRYPT_HASH_HANDLE* phHash,
    PUCHAR pbSecret,
    ULONG cbSecret,
    std::error_code& ec)
{
    using Fn = NTSTATUS(WINAPI*)(BCRYPT_ALG_HANDLE, BCRYPT_HASH_HANDLE*, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
    static Fn fn = nullptr;

    if (!Resolve("BCryptCreateHash", fn, ec))
    {
        return STATUS_UNSUCCESSFUL;
    }

    return fn(hAlgorithm, phHash, NULL, 0, pbSecret, cbSecret, 0);
}

NTSTATUS HashData(BCRYPT_HASH_HANDLE hHash, PUCHAR pbInput, ULONG cbInput, std::error_code& ec)
{
    using Fn = NTSTATUS(WINAPI*)(BCRYPT_HASH_HANDLE, PUCHAR, ULONG, ULONG);
    static Fn fn = nullptr;

    if (!Resolve("BCryptHashData", fn, ec))
    {
        return STATUS_UNSUCCESSFUL;
    }

    return fn(hHash, pbInput, cbInput, 0);
}

NTSTATUS FinishHash(BCRYPT_HASH_HANDLE hHash, PUCHAR pbOutput, ULONG cbOutput, std::error_code& ec)
{
    using Fn = NTSTATUS(WINAPI*)(BCRYPT_HASH_HANDLE, PUCHAR, ULONG, ULONG);
    static Fn fn = nullptr;

    if (!Resolve("BCryptFinishHash", fn, ec))
    {
        return STATUS_UNSUCCESSFUL;
    }

    return fn(hHash, pbOutput, cbOutput, 0);
}

NTSTATUS DestroyHash(BCRYPT_HASH_HANDLE hHash, std::error_code& ec)
{
    using Fn = NTSTATUS(WINAPI*)(BCRYPT_HASH_HANDLE);
    static Fn fn = nullptr;

    if (!Resolve("BCryptDestroyHash", fn, ec))
    {
        return STATUS_UNSUCCESSFUL;
    }

    return fn(hHash);
}

}  // namespace BCryptApi
}  // namespace Orc
