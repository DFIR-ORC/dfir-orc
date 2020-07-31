//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "Buffer.h"

#include <chrono>
#include <safeint.h>

namespace Orc {


template <
    typename To,
    typename From,
    std::enable_if_t<std::is_integral<To>::value, int > = 0,
    std::enable_if_t<std::is_integral<From>::value, int> = 0>
static To ConvertTo(From from)
{
    using TFrom = std::decay_t<From>;
    using TTo = std::decay_t<To>;

    static_assert(std::numeric_limits<TFrom>::is_integer, "ConvertTo From type is not an integer");
    static_assert(std::numeric_limits<TTo>::is_integer, "ConvertTo To type is not an integer");

    return msl::utilities::SafeInt<To>(from);
}


static FILETIME ConvertToFILETIME(LONGLONG fileTime)
{
    FILETIME retval;
    retval.dwLowDateTime = ((LARGE_INTEGER*)&fileTime)->LowPart;
    retval.dwHighDateTime = ((LARGE_INTEGER*)&fileTime)->HighPart;
    return retval;
}

static FILETIME ConvertToFILETIME(ULONGLONG fileTime)
{
    FILETIME retval;
    retval.dwLowDateTime = ((ULARGE_INTEGER*)&fileTime)->LowPart;
    retval.dwHighDateTime = ((ULARGE_INTEGER*)&fileTime)->HighPart;
    return retval;
}

static std::chrono::system_clock::time_point ConvertTo(const FILETIME& ft)
{

    using namespace std::chrono;
    using windows_duration = duration<int64_t, std::ratio<1, 10'000'000>>;
    constexpr windows_duration nt_to_unix_epoch {11644473600000LL * 10000LL};

    ULARGE_INTEGER uli;
    uli.HighPart = ft.dwHighDateTime;
    uli.LowPart = ft.dwLowDateTime;

    return std::chrono::system_clock::time_point(windows_duration(uli.QuadPart - nt_to_unix_epoch.count()));
}

static Orc::ByteBuffer ConvertBase64(const std::wstring_view strBase64, DWORD dwFlags = CRYPT_STRING_BASE64)
{
    Orc::ByteBuffer retval;
    DWORD dwRequiredSize = 0L;

    using namespace msl::utilities;

    if (!CryptStringToBinaryW(
            strBase64.data(), SafeInt<DWORD>(strBase64.size()), dwFlags, NULL, &dwRequiredSize, NULL, NULL))
    {
        if (auto error = GetLastError(); error != ERROR_MORE_DATA)
        {
            throw Exception(
                Continue, HRESULT_FROM_WIN32(error), L"Conversion from BASE64 failed to compute required size\r\n");
        }
    }
    retval.resize(dwRequiredSize);
    if (!CryptStringToBinaryW(
            strBase64.data(), SafeInt<DWORD>(strBase64.size()), dwFlags, retval.get(), &dwRequiredSize, NULL, NULL))
    {
        throw Exception(
            Continue,
            HRESULT_FROM_WIN32(GetLastError()),
            L"Conversion from BASE64 failed to compute required size\r\n");
    }
    return retval;
}

static Orc::ByteBuffer ConvertBase64(const std::string_view strBase64, DWORD dwFlags = CRYPT_STRING_BASE64)
{
    Orc::ByteBuffer retval;
    DWORD dwRequiredSize = 0L;

    using namespace msl::utilities;

    if (!CryptStringToBinaryA(
            strBase64.data(), SafeInt<DWORD>(strBase64.size()), dwFlags, NULL, &dwRequiredSize, NULL, NULL))
    {
        if (auto error = GetLastError(); error != ERROR_MORE_DATA)
        {
            throw Exception(
                Continue, HRESULT_FROM_WIN32(error), L"Conversion from BASE64 failed to compute required size\r\n");
        }
    }
    retval.resize(dwRequiredSize);
    if (!CryptStringToBinaryA(
            strBase64.data(), SafeInt<DWORD>(strBase64.size()), dwFlags, retval.get(), &dwRequiredSize, NULL, NULL))
    {
        throw Exception(
            Continue,
            HRESULT_FROM_WIN32(GetLastError()),
            L"Conversion from BASE64 failed to compute required size\r\n");
    }
    return retval;
}
template <size_t _DeclElts>
static std::wstring
ConvertToBase64(const Buffer<BYTE, _DeclElts>& buffer, DWORD dwFlags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF)
{
    DWORD dwRequiredSize = 0L;
    Orc::Buffer<WCHAR, 1024> strBuffer;

    using namespace msl::utilities;

    if (!CryptBinaryToStringW(buffer.get(), SafeInt<DWORD>(buffer.size()), dwFlags, NULL, &dwRequiredSize))
    {
        if (auto error = GetLastError(); error != ERROR_MORE_DATA)
            throw Exception(
                Continue, HRESULT_FROM_WIN32(error), L"Conversion from BASE64 failed to compute required size\r\n");
        else
            strBuffer.reserve(dwRequiredSize);
    }
    strBuffer.resize(dwRequiredSize);
    if (!CryptBinaryToStringW(buffer.get(), SafeInt<DWORD>(buffer.size()), dwFlags, strBuffer.get(), &dwRequiredSize))
    {
        throw Exception(
            Continue,
            HRESULT_FROM_WIN32(GetLastError()),
            L"Conversion from BASE64 failed to compute required size\r\n");
    }
    return std::wstring(strBuffer.get(), dwRequiredSize);
}

}  // namespace Orc
