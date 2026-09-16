//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Text/CryptString.h"

#include <algorithm>

#include <wincrypt.h>

#include "Log/Log.h"

namespace {

// One CryptBinaryToStringW conversion with exactly 'flags', size query included.
Orc::Result<std::wstring> CallCryptBinaryToString(Orc::BufferView data, DWORD flags)
{
    using namespace Orc;

    if (data.size() > MAXDWORD)
    {
        Log::Debug(L"CryptBinaryToStringW: {} bytes exceed what it takes", data.size());
        return std::make_error_code(std::errc::value_too_large);
    }

    const DWORD size = static_cast<DWORD>(data.size());

    DWORD length = 0;
    if (!CryptBinaryToStringW(data.data(), size, flags, nullptr, &length) || length == 0)
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed CryptBinaryToStringW size query (flags: {:#x}) [{}]", flags, ec);
        return ec;
    }

    std::wstring text(length, L'\0');
    if (!CryptBinaryToStringW(data.data(), size, flags, text.data(), &length))
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed CryptBinaryToStringW (flags: {:#x}) [{}]", flags, ec);
        return ec;
    }

    text.resize(length);  // on success 'length' excludes the terminating NUL
    return text;
}

std::wstring ToSpacedHex(Orc::BufferView data)
{
    static const wchar_t kDigits[] = L"0123456789abcdef";

    std::wstring text;
    text.reserve(data.size() * 3);
    for (const uint8_t byte : data)
    {
        if (!text.empty())
        {
            text += L' ';
        }

        text += kDigits[byte >> 4];
        text += kDigits[byte & 0x0F];
    }

    return text;
}

Orc::Result<std::wstring> ToBase64WithoutLineBreaks(Orc::BufferView data)
{
    auto text = CallCryptBinaryToString(data, CRYPT_STRING_BASE64);
    if (text.has_error())
    {
        return text;
    }

    text->erase(
        std::remove_if(text->begin(), text->end(), [](wchar_t c) { return c == L'\r' || c == L'\n'; }), text->end());
    return text;
}

}  // namespace

namespace Orc {

Result<std::wstring> CryptBinaryToSingleLineString(BufferView data, DWORD format)
{
    if (data.empty())
    {
        return std::wstring();
    }

    auto text = CallCryptBinaryToString(data, format | CRYPT_STRING_NOCRLF);
    if (text.has_value())
    {
        return text;
    }

    Log::Debug(L"CRYPT_STRING_NOCRLF refused, building the text without it [{}]", text.error());
    return BinaryToSingleLineString(data, format);
}

Result<std::wstring> BinaryToSingleLineString(BufferView data, DWORD format)
{
    if (data.empty())
    {
        return std::wstring();
    }

    const DWORD base = format & ~static_cast<DWORD>(CRYPT_STRING_NOCRLF);
    if (base == CRYPT_STRING_BASE64)
    {
        return ToBase64WithoutLineBreaks(data);
    }

    if (base == CRYPT_STRING_HEX)
    {
        return ToSpacedHex(data);
    }

    Log::Debug(L"BinaryToSingleLineString: unsupported format {:#x}", format);
    return std::make_error_code(std::errc::invalid_argument);
}

}  // namespace Orc
