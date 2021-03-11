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

constexpr std::wstring_view kUtf8 = L"utf-8";
constexpr std::wstring_view kUtf16 = L"utf-16";
constexpr std::wstring_view kUnknown = L"unknown";

std::wstring_view ToString(Text::Encoding encoding)
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

Result<Text::Encoding> ToEncoding(const std::wstring& disposition)
{
    const std::map<std::wstring_view, Text::Encoding> map = {
        {kUtf8, Text::Encoding::Utf8}, {kUtf16, Text::Encoding::Utf16}};

    auto it = map.find(disposition);
    if (it == std::cend(map))
    {
        return std::make_error_code(std::errc::invalid_argument);
    }

    return it->second;
}

}  // namespace Orc
