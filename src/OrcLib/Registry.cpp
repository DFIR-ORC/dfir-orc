//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "Registry.h"

using namespace std::string_view_literals;

namespace Orc {

template <>
Result<ULONG32> Orc::Registry::Read<ULONG32>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName)
{
    HKEY hKey = nullptr;

    if (szKeyName)
    {
        if (auto status = RegOpenKeyExW(hParentKey, szKeyName, REG_OPTION_OPEN_LINK, KEY_QUERY_VALUE, &hKey);
            status != ERROR_SUCCESS)
        {
            const auto ec = Win32Error(status);
            Log::Debug(L"Failed to open registry key {} [{}]", szKeyName, ec);
            return ec;
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
        const auto ec = Win32Error(status);

        if (status == ERROR_MORE_DATA)
        {
            Log::Error(L"Unexpected registry value '{}' is bigger than expected (ULONG32)", szValueName);
            return ec;
        }
        else
        {
            Log::Debug(L"Failed to open registry value '{}' [{}]", szValueName, ec);
            return ec;
        }
    }
    else
    {
        valueBuffer.use((cbBytes / sizeof(ULONG32)));
    }

    if (dwValueType != REG_DWORD || (dwValueType != REG_BINARY && cbBytes != sizeof(ULONG32)))
    {
        Log::Error(L"Unexpected value '{}' type (not ULONG32 compatible)", szValueName);
        return Win32Error(ERROR_DATATYPE_MISMATCH);
    }

    return valueBuffer[0];
}

template <>
Result<ULONG64> Orc::Registry::Read<ULONG64>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName)
{
    HKEY hKey = nullptr;

    if (szKeyName)
    {
        if (auto status = RegOpenKeyExW(hParentKey, szKeyName, REG_OPTION_OPEN_LINK, KEY_QUERY_VALUE, &hKey);
            status != ERROR_SUCCESS)
        {
            const auto ec = Win32Error(status);
            Log::Debug(L"Failed to open registry key {} [{}]", szKeyName, ec);
            return ec;
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
        const auto ec = Win32Error(status);
        if (status == ERROR_MORE_DATA)
        {
            Log::Error(L"Unexpected registry value '{}' is bigger than expected (ULONG32)", szValueName);
            return ec;
        }
        else
        {
            Log::Debug(L"Failed to query registry '{}' value [{}]", szValueName, ec);
            return ec;
        }
    }
    else
    {
        valueBuffer.use((cbBytes / sizeof(ULONG64)));
    }
    if (dwValueType != REG_QWORD || (dwValueType != REG_BINARY && cbBytes != sizeof(ULONG64)))
    {
        Log::Error(L"Unexpected value '{}' type (not ULONG32 compatible)", szValueName);
        return Win32Error(ERROR_DATATYPE_MISMATCH);
    }

    return valueBuffer[0];
}

template <>
Result<Orc::ByteBuffer> Orc::Registry::Read<Orc::ByteBuffer>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName)
{
    HKEY hKey = nullptr;

    if (szKeyName)
    {
        if (auto status = RegOpenKeyExW(hParentKey, szKeyName, REG_OPTION_OPEN_LINK, KEY_QUERY_VALUE, &hKey);
            status != ERROR_SUCCESS)
        {
            const auto ec = Win32Error(status);
            Log::Debug(L"Failed to open registry key {} [{}]", szKeyName, ec);
            return ec;
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
        const auto ec = Win32Error(status);

        if (status == ERROR_MORE_DATA)
        {
            valueBuffer.resize(cbBytes / sizeof(BYTE));
            if (auto status = RegQueryValueExW(hKey, szValueName, NULL, NULL, (LPBYTE)valueBuffer.get(), &cbBytes);
                status != ERROR_SUCCESS)
            {
                Log::Debug(
                    L"Failed to query registry value '{}:{}' value [{}]", szKeyName ? szKeyName : L"", szValueName, ec);
                return ec;
            }
        }
        else
        {
            Log::Debug(
                L"Failed to query registry value '{}:{}' value [{}]", szKeyName ? szKeyName : L"", szValueName, ec);
            return ec;
        }
    }
    else
    {
        valueBuffer.use((cbBytes / sizeof(BYTE)));
    }
    return valueBuffer;
}

template <>
Result<std::wstring> Orc::Registry::Read<std::wstring>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName)
{
    HKEY hKey = nullptr;

    if (szKeyName)
    {
        if (auto status = RegOpenKeyExW(hParentKey, szKeyName, REG_OPTION_OPEN_LINK, KEY_QUERY_VALUE, &hKey);
            status != ERROR_SUCCESS)
        {
            const auto result = Win32Error(status);
            Log::Debug(L"Failed to open registry key '{}' [{}]", szKeyName, result);
            return result;
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
        const auto ec = Win32Error(status);

        if (status == ERROR_MORE_DATA)
        {
            valueBuffer.resize(cbBytes / sizeof(WCHAR));
            if (auto status = RegQueryValueExW(hKey, szValueName, NULL, NULL, (LPBYTE)valueBuffer.get(), &cbBytes);
                status != ERROR_SUCCESS)
            {
                Log::Debug(L"Failed to query registry value '{}' value [{}])", szValueName, ec);
                return ec;
            }
        }
        else
        {
            Log::Debug(L"Failed to query registry value '{}' [{}]", szValueName, ec);
            return ec;
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

        return std::wstring(valueBuffer.get(), valueBuffer.size());
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
                throw Exception(Severity::Continue, E_FAIL, L"Unexpected return value for ExpandEnvironmentStringsW"sv);
            }
        }
        else
        {
            expandBuffer.use(cbSize);
        }
        return std::wstring(expandBuffer.get(), expandBuffer.size());
    }
    else
    {
        Log::Error(L"Registry value {} not of the expected (REG_*SZ) type", szValueName);
        return SystemError(HRESULT_FROM_WIN32(ERROR_DATATYPE_MISMATCH));
    }
}

template <>
Result<std::filesystem::path>
Orc::Registry::Read<std::filesystem::path>(HKEY hParentKey, LPWSTR szKeyName, LPWSTR szValueName)
{
    auto result = Read<std::wstring>(hParentKey, szKeyName, szValueName);
    if (result.has_error())
    {
        return result.error();
    }

    return std::filesystem::path(*result);
}

}  // namespace Orc
