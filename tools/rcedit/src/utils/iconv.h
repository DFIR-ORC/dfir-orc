//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <optional>
#include <string>
#include <system_error>

namespace rcedit {

std::string Utf16ToUtf8( const std::wstring& utf16, std::error_code& ec );

std::string Utf16ToUtf8( const std::wstring& utf16 );

std::wstring Utf8ToUtf16( const std::string& utf8, std::error_code& ec );

std::wstring Utf8ToUtf16( const std::string& utf8 );

std::optional< std::wstring > Utf8ToUtf16(
    const std::optional< std::string >& utf8 );

}  // namespace rcedit
