//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "Encoding.h"

#include <string>

namespace Orc {

constexpr std::string_view kUtf8 = "utf-8";
constexpr std::string_view kUtf16 = "utf-16";
constexpr std::string_view kUnknown = "unknown";

constexpr std::wstring_view kUtf8W = L"utf-8";
constexpr std::wstring_view kUtf16W = L"utf-16";
constexpr std::wstring_view kUnknownW = L"unknown";

std::string_view ToString(Text::Encoding encoding)
{
    switch (encoding)
    {
        case Text::Encoding::Utf8:
            return kUtf8;
        case Text::Encoding::Utf16:
            return kUtf16;
        default:
            return kUnknown;
    }
}

std::wstring_view ToWString(Text::Encoding encoding)
{
    switch (encoding)
    {
        case Text::Encoding::Utf8:
            return kUtf8W;
        case Text::Encoding::Utf16:
            return kUtf16W;
        default:
            return kUnknownW;
    }
}

Result<Text::Encoding> ToEncoding(const std::wstring& disposition)
{
    const std::map<std::wstring_view, Text::Encoding> map = {
        {kUtf8W, Text::Encoding::Utf8}, {kUtf16W, Text::Encoding::Utf16}};

    auto it = map.find(disposition);
    if (it == std::cend(map))
    {
        return std::make_error_code(std::errc::invalid_argument);
    }

    return it->second;
}

}  // namespace Orc
