//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <string>
#include <system_error>
#include <type_traits>

#include "Utils/TypeTraits.h"

namespace Orc {

constexpr auto kFailedConversion = std::string_view("<encoding_error>");
constexpr auto kFailedConversionW = std::wstring_view(L"<encoding_error>");

std::string Utf16ToUtf8(std::wstring_view utf16, std::error_code& ec);
std::wstring Utf8ToUtf16(std::string_view utf8, std::error_code& ec);

std::wstring Utf8ToUtf16(std::string_view utf8);
std::string Utf16ToUtf8(std::wstring_view utf16);

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
