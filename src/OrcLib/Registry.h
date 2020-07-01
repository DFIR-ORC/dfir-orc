//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "OrcResult.h"
#include "Buffer.h"

#include <optional>
#include <filesystem>
#include <Windows.h>
#include "boost/scope_exit.hpp"

#pragma managed(push, off)


namespace Orc {

class LogFileWriter;

class ORCLIB_API Registry
{
public:
    template <typename _T>
    static stx::Result<_T, HRESULT> Read(const logger& pLog, HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName) {
        static_assert(false, "Registry read for your type must be a specialised version");
    }
};


template <>
stx::Result<ULONG32, HRESULT>
Orc::Registry::Read<ULONG32>(const logger& pLog, HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName);

template <>
stx::Result<ULONG64, HRESULT>
Orc::Registry::Read<ULONG64>(const logger& pLog, HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName);

template <>
stx::Result<ByteBuffer, HRESULT>
Orc::Registry::Read<ByteBuffer>(const logger& pLog, HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName);

template <>
stx::Result<std::wstring, HRESULT>
Orc::Registry::Read<std::wstring>(const logger& pLog, HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName);

template <>
stx::Result<std::vector<std::wstring>, HRESULT> Orc::Registry::Read<std::vector<std::wstring>>(
    const logger& pLog,
    HKEY hParentKey,
    LPWSTR szKeyName,
    LPWSTR szValueName);

template <>
stx::Result<std::filesystem::path, HRESULT>
Orc::Registry::Read<std::filesystem::path>(const logger& pLog, HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName);

} // namespace Orc


#pragma managed(pop)
