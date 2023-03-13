//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "Text/Iconv.h"

#include <Windows.h>
#include <assert.h>

#include <string_view>

namespace Orc {

std::string ToUtf8(std::wstring_view utf16, std::error_code& ec)
{
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

std::wstring ToUtf16(const std::string_view utf8, std::error_code& ec)
{
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

std::wstring ToUtf16(std::string_view utf8)
{
    std::error_code ec;
    const auto utf16 = Orc::ToUtf16(utf8, ec);
    if (ec)
    {
        return std::wstring {Orc::kFailedConversionW};
    }

    return utf16;
}

std::string ToUtf8(std::wstring_view utf16)
{
    std::error_code ec;
    const auto utf8 = Orc::ToUtf8(utf16, ec);
    if (ec)
    {
        return std::string {Orc::kFailedConversion};
    }

    return utf8;
}

}  // namespace Orc
