//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "Registry.h"

using namespace stx;

template <>
stx::Result<ULONG32, HRESULT> Orc::Registry::Read<ULONG32>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName)
{
    HKEY hKey = nullptr;

    if (szKeyName)
    {
        if (auto status = RegOpenKeyExW(hParentKey, szKeyName, REG_OPTION_OPEN_LINK, KEY_QUERY_VALUE, &hKey);
            status != ERROR_SUCCESS)
        {
            Log::Error(L"Failed to registry key {} (code: {:#x})", szKeyName, HRESULT_FROM_WIN32(status));
            return Err(HRESULT_FROM_WIN32(status));
        }
    }
    else
    {
        hKey = hParentKey;
    }

    BOOST_SCOPE_EXIT(&hKey, &hParentKey)
    {
        if (hKey != hParentKey)
            RegCloseKey(hKey);
    }
    BOOST_SCOPE_EXIT_END;

    Buffer<ULONG32, 1> valueBuffer;
    DWORD cbBytes = sizeof(ULONG32);
    valueBuffer.reserve(1);
    DWORD dwValueType = 0;

    if (auto status = RegQueryValueExW(hKey, szValueName, NULL, &dwValueType, (LPBYTE)valueBuffer.get(), &cbBytes);
        status != ERROR_SUCCESS)
    {
        if (status == ERROR_MORE_DATA)
        {
            Log::Error(L"Unexepected registry value \"{}\" is bigger than expected (ULONG32)", szValueName);
            return Err(HRESULT_FROM_WIN32(status));
        }
        else
        {
            Log::Error(L"Failed to open registry \"{}\" value (code: {:#x})", szValueName, HRESULT_FROM_WIN32(status));
            return Err(HRESULT_FROM_WIN32(status));
        }
    }
    else
    {
        valueBuffer.use((cbBytes / sizeof(ULONG32)));
    }

    if (dwValueType != REG_DWORD || (dwValueType != REG_BINARY && cbBytes != sizeof(ULONG32)))
    {
        Log::Error(L"Unexpected value \"{}\" type (not ULONG32 compatible)", szValueName);
        return Err(HRESULT_FROM_WIN32(ERROR_DATATYPE_MISMATCH));
    }

    return Ok<ULONG32>(std::move(valueBuffer[0]));
}

template <>
stx::Result<ULONG64, HRESULT> Orc::Registry::Read<ULONG64>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName)
{
    HKEY hKey = nullptr;

    if (szKeyName)
    {
        if (auto status = RegOpenKeyExW(hParentKey, szKeyName, REG_OPTION_OPEN_LINK, KEY_QUERY_VALUE, &hKey);
            status != ERROR_SUCCESS)
        {
            Log::Error(L"Failed to registry key {} (code: {:#x})", szKeyName, HRESULT_FROM_WIN32(status));
            return Err(HRESULT_FROM_WIN32(status));
        }
    }
    else
    {
        hKey = hParentKey;
    }

    BOOST_SCOPE_EXIT(&hKey, &hParentKey)
    {
        if (hKey != hParentKey)
            RegCloseKey(hKey);
    }
    BOOST_SCOPE_EXIT_END;

    Buffer<ULONG64, 1> valueBuffer;
    DWORD cbBytes = sizeof(ULONG64);
    valueBuffer.reserve(1);
    DWORD dwValueType = 0;

    if (auto status = RegQueryValueExW(hKey, szValueName, NULL, &dwValueType, (LPBYTE)valueBuffer.get(), &cbBytes);
        status != ERROR_SUCCESS)
    {
        if (status == ERROR_MORE_DATA)
        {
            Log::Error(L"Unexepected registry value \"{}\" is bigger than expected (ULONG32)", szValueName);
            return Err(HRESULT_FROM_WIN32(status));
        }
        else
        {
            Log::Error(L"Failed to open registry \"{}\" value (code: {:#x})", szValueName, HRESULT_FROM_WIN32(status));
            return Err(HRESULT_FROM_WIN32(status));
        }
    }
    else
    {
        valueBuffer.use((cbBytes / sizeof(ULONG64)));
    }
    if (dwValueType != REG_QWORD || (dwValueType != REG_BINARY && cbBytes != sizeof(ULONG64)))
    {
        Log::Error(L"Unexpected value \"{}\" type (not ULONG32 compatible)", szValueName);
        return Err(HRESULT_FROM_WIN32(ERROR_DATATYPE_MISMATCH));
    }

    return Ok(std::move(valueBuffer[0]));
}

template <>
stx::Result<Orc::ByteBuffer, HRESULT>
Orc::Registry::Read<Orc::ByteBuffer>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName)
{
    HKEY hKey = nullptr;

    if (szKeyName)
    {
        if (auto status = RegOpenKeyExW(hParentKey, szKeyName, REG_OPTION_OPEN_LINK, KEY_QUERY_VALUE, &hKey);
            status != ERROR_SUCCESS)
        {
            Log::Error(L"Failed to registry key {} (code: {:#x})", szKeyName, HRESULT_FROM_WIN32(status));
            return Err(HRESULT_FROM_WIN32(status));
        }
    }
    else
    {
        hKey = hParentKey;
    }

    BOOST_SCOPE_EXIT(&hKey, &hParentKey)
    {
        if (hKey != hParentKey)
            RegCloseKey(hKey);
    }
    BOOST_SCOPE_EXIT_END;

    ByteBuffer valueBuffer;
    DWORD cbBytes = valueBuffer.inner_elts() * sizeof(BYTE);
    valueBuffer.reserve(valueBuffer.inner_elts());
    DWORD dwValueType = 0;
    if (auto status = RegQueryValueExW(hKey, szValueName, NULL, &dwValueType, (LPBYTE)valueBuffer.get(), &cbBytes);
        status != ERROR_SUCCESS)
    {
        if (status == ERROR_MORE_DATA)
        {
            valueBuffer.resize(cbBytes / sizeof(BYTE));
            if (auto status = RegQueryValueExW(hKey, szValueName, NULL, NULL, (LPBYTE)valueBuffer.get(), &cbBytes);
                status != ERROR_SUCCESS)
            {
                Log::Error(
                    L"Failed to reg value \"{}:{}\" value (code: {:#x})",
                    szKeyName ? szKeyName : L"",
                    szValueName,
                    HRESULT_FROM_WIN32(status));
                return Err(HRESULT_FROM_WIN32(status));
            }
        }
        else
        {
            Log::Error(
                L"Failed to reg value \"{}:{}\" value (code: {:#x})",
                szKeyName ? szKeyName : L"",
                szValueName,
                HRESULT_FROM_WIN32(status));
            return Err(HRESULT_FROM_WIN32(status));
        }
    }
    else
    {
        valueBuffer.use((cbBytes / sizeof(BYTE)));
    }
    return Ok(std::move(valueBuffer));
}

template <>
stx::Result<std::wstring, HRESULT>
Orc::Registry::Read<std::wstring>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName)
{
    HKEY hKey = nullptr;

    if (szKeyName)
    {
        if (auto status = RegOpenKeyExW(hParentKey, szKeyName, REG_OPTION_OPEN_LINK, KEY_QUERY_VALUE, &hKey);
            status != ERROR_SUCCESS)
        {
            Log::Error(L"Failed to registry key '{}' (code: {:#x})", szKeyName, HRESULT_FROM_WIN32(status));
            return Err(HRESULT_FROM_WIN32(status));
        }
    }
    else
    {
        hKey = hParentKey;
    }

    BOOST_SCOPE_EXIT(&hKey, &hParentKey)
    {
        if (hKey != hParentKey)
            RegCloseKey(hKey);
    }
    BOOST_SCOPE_EXIT_END;

    Buffer<WCHAR, MAX_PATH> valueBuffer;
    DWORD cbBytes = MAX_PATH * sizeof(WCHAR);
    valueBuffer.reserve(MAX_PATH);
    DWORD dwValueType = 0;
    if (auto status = RegQueryValueExW(hKey, szValueName, NULL, &dwValueType, (LPBYTE)valueBuffer.get(), &cbBytes);
        status != ERROR_SUCCESS)
    {
        if (status == ERROR_MORE_DATA)
        {
            valueBuffer.resize(cbBytes / sizeof(WCHAR));
            if (auto status = RegQueryValueExW(hKey, szValueName, NULL, NULL, (LPBYTE)valueBuffer.get(), &cbBytes);
                status != ERROR_SUCCESS)
            {
                Log::Error(L"Failed to reg value \"{}\" value (code: {:#x})", szValueName, HRESULT_FROM_WIN32(status));
                return Err(HRESULT_FROM_WIN32(status));
            }
        }
        else
        {
            Log::Error(
                L"Failed to open profile list \"{}\" value (code: {:#x})", szValueName, HRESULT_FROM_WIN32(status));
            return Err(HRESULT_FROM_WIN32(status));
        }
    }
    else
    {
        valueBuffer.use((cbBytes / sizeof(WCHAR)));
    }

    if (dwValueType == REG_SZ)
    {
        if (valueBuffer.size()
            >= 1)  // If string is not empty, we need to get rid of the trailing \0 to build a wstring
        {
            valueBuffer.resize(valueBuffer.size() - 1);
        }

        return Ok(std::wstring(valueBuffer.get(), valueBuffer.size()));
    }
    else if (dwValueType == REG_EXPAND_SZ)
    {
        Buffer<WCHAR, MAX_PATH> expandBuffer;
        expandBuffer.reserve(MAX_PATH);

        if (auto cbSize = ExpandEnvironmentStringsW(valueBuffer.get(), expandBuffer.get(), MAX_PATH); cbSize > MAX_PATH)
        {
            expandBuffer.reserve(cbSize);
            if (auto cbSizeReTry = ExpandEnvironmentStringsW(valueBuffer.get(), expandBuffer.get(), cbSize);
                cbSizeReTry == cbSize)
            {
                expandBuffer.use(cbSize);
            }
            else
            {
                throw Exception(Severity::Continue, E_FAIL, L"Unexpected return value for ExpandEnvironmentStringsW");
            }
        }
        else
        {
            expandBuffer.use(cbSize);
        }
        return Ok(std::wstring(expandBuffer.get(), expandBuffer.size()));
    }
    else
    {
        Log::Error(L"Registry value {} not of the expected (REG_*SZ) type", szValueName);
        return Err(HRESULT_FROM_WIN32(ERROR_DATATYPE_MISMATCH));
    }
}

template <>
stx::Result<std::filesystem::path, HRESULT>
Orc::Registry::Read<std::filesystem::path>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName)
{
    return Read<std::wstring>(hParentKey, szKeyName, szValueName).and_then([](auto&& value) {
        return std::filesystem::path(std::move(value));
    });
}
