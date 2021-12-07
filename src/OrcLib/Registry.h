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

#include "Utils/Result.h"
#include "Buffer.h"

#pragma managed(push, off)

namespace Orc {

class Registry
{
public:
    template <typename _T>
    static Result<_T> Read(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName)
    {
        static_assert(false, "Registry read for your type must be a specialised version");
    }
};

template <>
Result<ULONG32> Orc::Registry::Read<ULONG32>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName);

template <>
Result<ULONG64> Orc::Registry::Read<ULONG64>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName);

template <>
Result<ByteBuffer> Orc::Registry::Read<ByteBuffer>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName);

template <>
Result<std::wstring> Orc::Registry::Read<std::wstring>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName);

template <>
Result<std::vector<std::wstring>>
Orc::Registry::Read<std::vector<std::wstring>>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName);

template <>
Result<std::filesystem::path>
Orc::Registry::Read<std::filesystem::path>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName);

}  // namespace Orc

#pragma managed(pop)
