//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <optional>

#include <filesystem>
#include <Windows.h>

#include <boost/scope_exit.hpp>

#include "OrcResult.h"
#include "Buffer.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API Registry
{
public:
    template <typename _T>
    static stx::Result<_T, HRESULT> Read(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName)
    {
        static_assert(false, "Registry read for your type must be a specialised version");
    }
};


template <>
stx::Result<ULONG32, HRESULT> Orc::Registry::Read<ULONG32>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName);

template <>
stx::Result<ULONG64, HRESULT> Orc::Registry::Read<ULONG64>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName);

template <>
stx::Result<ByteBuffer, HRESULT> Orc::Registry::Read<ByteBuffer>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName);

template <>
stx::Result<std::wstring, HRESULT>
Orc::Registry::Read<std::wstring>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName);

template <>
stx::Result<std::vector<std::wstring>, HRESULT>
Orc::Registry::Read<std::vector<std::wstring>>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName);

template <>
stx::Result<std::filesystem::path, HRESULT>
Orc::Registry::Read<std::filesystem::path>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName);

} // namespace Orc

#pragma managed(pop)
