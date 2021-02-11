//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "stdafx.h"

#include "Text/Iconv.h"

#include <string_view>

namespace Orc {

std::string Utf16ToUtf8(LPCWSTR utf16, std::error_code& ec)
{
    return Utf16ToUtf8(std::wstring_view(utf16), ec);
}

std::string Utf16ToUtf8(LPWSTR utf16, std::error_code& ec)
{
    return Utf16ToUtf8(std::wstring_view(utf16), ec);
}

std::wstring Utf8ToUtf16(LPCSTR utf8, std::error_code& ec)
{
    return Utf8ToUtf16(std::string_view(utf8), ec);
}

std::wstring Utf8ToUtf16(LPSTR utf8, std::error_code& ec)
{
    return Utf8ToUtf16(std::string_view(utf8), ec);
}

}  // namespace Orc
