//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <string_view>
#include <string>

namespace Orc {
namespace Text {

enum class Encoding
{
    Unknown,
    Utf8,
    Utf16,
};

}  // namespace Text

std::wstring_view ToString(Text::Encoding encoding);
Text::Encoding ToEncoding(const std::wstring& encoding, std::error_code& ec = std::error_code());

}  // namespace Orc
