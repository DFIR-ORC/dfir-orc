//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include "Utils/Fwd/Iconv.h"

#include <string>
#include <system_error>
#include <type_traits>

#include <Windows.h>
#include <assert.h>

#include "Utils/TypeTraits.h"

namespace Orc {

std::string Utf16ToUtf8(LPCWSTR utf16, std::error_code& ec);
std::string Utf16ToUtf8(LPWSTR utf16, std::error_code& ec);

std::wstring Utf8ToUtf16(LPCSTR utf16, std::error_code& ec);
std::wstring Utf8ToUtf16(LPSTR utf16, std::error_code& ec);

template <typename T>
std::string Utf16ToUtf8(const T& utf16, std::error_code& ec)
{
    static_assert(std::is_same_v<typename T::value_type, wchar_t>, "Utf16ToUtf8 expected input value_type: 'wchar_t'");

    if (utf16.size() == 0)
    {
        return {};
    }

    //
    // From MSDN:
    //
    // If this parameter is -1, the function processes the entire input string,
    // including the terminating null character. Therefore, the resulting
    // character string has a terminating null character, and the length
    // returned by the function includes this character.
    //
    const auto requiredSize =
        WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), NULL, 0, NULL, NULL);

    if (requiredSize == 0)
    {
        ec.assign(GetLastError(), std::system_category());
        return {};
    }

    std::string utf8;
    utf8.resize(requiredSize);

    const auto converted = WideCharToMultiByte(
        CP_UTF8,
        0,
        utf16.data(),
        static_cast<int>(utf16.size()),
        utf8.data(),
        static_cast<int>(utf8.size()),
        NULL,
        NULL);

    if (converted == 0)
    {
        ec.assign(GetLastError(), std::system_category());
        return {};
    }

    assert(converted == requiredSize);
    return utf8;
}

template <typename T>
std::wstring Utf8ToUtf16(const T& utf8, std::error_code& ec)
{
    static_assert(std::is_same_v<typename T::value_type, char>, "Utf8ToUtf16 expected input value_type: 'char'");

    if (utf8.size() == 0)
    {
        return {};
    }

    //
    // From MSDN:
    //
    // If this parameter is -1, the function processes the entire input string,
    // including the terminating null character. Therefore, the resulting
    // character string has a terminating null character, and the length
    // returned by the function includes this character.
    //
    const auto requiredSize = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), NULL, 0);

    if (requiredSize == 0)
    {
        ec.assign(GetLastError(), std::system_category());
        return {};
    }

    std::wstring utf16;
    utf16.resize(requiredSize);

    const auto converted = MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), utf16.data(), static_cast<int>(utf16.size()));

    if (converted == 0)
    {
        ec.assign(GetLastError(), std::system_category());
        return {};
    }

    assert(converted == requiredSize);
    return utf16;
}

template <typename OutputCharType, typename Input>
decltype(auto) EncodeTo(Input&& input)
{
    using InputCharT = Traits::underlying_char_type_t<Input>;
    using OutputCharT = Traits::underlying_char_type_t<OutputCharType>;

    if constexpr (std::is_same_v<InputCharT, OutputCharT>)
    {
        return std::forward<Input>(input);
    }
    else if constexpr (std::is_same_v<InputCharT, char>)
    {
        std::error_code ec;
        return Utf8ToUtf16(input, ec);
    }
    else if constexpr (std::is_same_v<InputCharT, wchar_t>)
    {
        std::error_code ec;
        return Utf16ToUtf8(input, ec);
    }
    else
    {
        static_assert(true, "Unsupported 'Input' type");
        return std::forward<Input>(input);
    }
}

//
// Convert 'Input' into 'OutputCharType' if different or forward original 'Input'
//
template <typename OutputCharType, typename Input>
decltype(auto) TryEncodeTo(Input&& input)
{
    if constexpr (Traits::is_char_underlying_type_v<Input>)
    {
        return EncodeTo<OutputCharType>(std::forward<Input>(input));
    }
    else
    {
        return std::forward<Input>(input);
    }
}

}  // namespace Orc
