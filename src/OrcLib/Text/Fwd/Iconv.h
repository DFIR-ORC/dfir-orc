//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

namespace Orc {

constexpr auto kFailedConversion = "<failed_conversion>";
constexpr auto kFailedConversionW = L"<failed_conversion>";

template <typename T>
std::string Utf16ToUtf8(const T& utf16, const std::string& onError);

template <typename T>
std::wstring Utf8ToUtf16(const T& utf16, const std::wstring& onError);

}  // namespace Orc
