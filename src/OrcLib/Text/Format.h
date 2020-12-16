//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

//
// Wrappers on fmt to add some flexibility on encoding conversions and formatting
//

#pragma once

#include <type_traits>

#include <fmt/core.h>

#include "Utils/Iconv.h"
#include "Utils/TypeTraits.h"

namespace Orc {
namespace Text {

namespace details {

template <class...>
constexpr std::false_type always_false {};

template <typename T, typename OutputIt>
void Utf8ToUtf16(const T& utf8, OutputIt out)
{
    std::error_code ec;
    auto utf16 = Orc::Utf8ToUtf16(utf8, ec);
    if (ec)
    {
        using namespace std::literals;

        static constexpr auto failed = L"<failed to convert to utf16>"sv;
        std::copy(std::cbegin(failed), std::cend(failed), out);
        return;
    }

    std::copy(std::cbegin(utf16), std::cend(utf16), out);
}

template <typename T, typename OutputIt>
void Utf16ToUtf8(const T& utf16, OutputIt out)
{
    std::error_code ec;
    auto utf8 = Orc::Utf16ToUtf8(utf16, ec);
    if (ec)
    {
        using namespace std::literals;

        static constexpr auto failed = "<failed to convert to utf8>"sv;
        std::copy(std::cbegin(failed), std::cend(failed), out);
        return;
    }

    std::copy(std::cbegin(utf8), std::cend(utf8), out);
}

}  // namespace details

template <typename OutputIt, typename FmtArg0, typename... FmtArgs>
void FormatWithEncodingTo(OutputIt out, FmtArg0&& arg0, FmtArgs&&... args)
{
    using BufferCharT = Traits::underlying_char_type_t<OutputIt>;
    using FmtCharT = Traits::underlying_char_type_t<FmtArg0>;

    if constexpr (std::is_same_v<FmtCharT, BufferCharT>)
    {
        fmt::format_to(out, std::forward<FmtArg0>(arg0), std::forward<FmtArgs>(args)...);
    }
    else if constexpr (std::is_same_v<FmtCharT, char>)
    {
        fmt::basic_memory_buffer<char, 32768> utf8;
        fmt::format_to(utf8, std::forward<FmtArg0>(arg0), std::forward<FmtArgs>(args)...);
        details::Utf8ToUtf16(utf8, out);
    }
    else if constexpr (std::is_same_v<FmtCharT, wchar_t>)
    {
        fmt::basic_memory_buffer<wchar_t, 32768> utf16;
        fmt::format_to(utf16, std::forward<FmtArg0>(arg0), std::forward<FmtArgs>(args)...);
        details::Utf16ToUtf8(utf16, out);
    }
    else
    {
        static_assert(details::always_false<OutputIt>, "Unhandled FmtArg0 type");
    }
}

template <typename OutputIt, typename RawArg>
void FormatWithEncodingTo(OutputIt out, RawArg&& arg)
{
    using BufferCharT = Traits::underlying_char_type_t<OutputIt>;
    using RawCharT = Traits::underlying_char_type_t<RawArg>;

    if constexpr (std::is_same_v<RawCharT, BufferCharT>)
    {
        std::copy(std::cbegin(arg), std::cend(arg), out);
    }
    else if constexpr (std::is_same_v<RawCharT, char>)
    {
        details::Utf8ToUtf16(arg, out);
    }
    else if constexpr (std::is_same_v<RawCharT, wchar_t>)
    {
        details::Utf16ToUtf8(arg, out);
    }
    else
    {
        static_assert(details::always_false<OutputIt>, "Unhandled arg type");
    }
}

// Wrapper on fmt::format_to which provides:
//   - conversion of FmtArgs arguments to FmtArg0 encoding
//   - conversion of fmt::format_to output to container's encoding
template <typename OutputIt, typename FmtArg0, typename... FmtArgs>
void FormatToWithoutEOL(OutputIt out, FmtArg0&& arg0, FmtArgs&&... args)
{
    // Use TryConvertToEncoding to process char/wchar_t conversion to FmtArg0's value_type
    using FmtCharT = Traits::underlying_char_type_t<FmtArg0>;
    FormatWithEncodingTo(out, std::forward<FmtArg0>(arg0), TryEncodeTo<FmtCharT>(args)...);
}

// Wrapper on fmt::format_to which provides:
//   - EOL character
//   - conversion of FmtArgs arguments to FmtArg0 encoding
//   - conversion of fmt::format_to output to container's encoding
template <typename OutputIt, typename FmtArg0, typename... FmtArgs>
void FormatTo(OutputIt out, FmtArg0&& arg0, FmtArgs&&... args)
{
    using FmtCharT = Traits::underlying_char_type_t<FmtArg0>;
    FormatToWithoutEOL(out, std::forward<FmtArg0>(arg0), std::forward<FmtArgs>(args)...);
    out++ = Traits::newline_v<Traits::underlying_char_type_t<OutputIt>>;
}

}  // namespace Text
}  // namespace Orc
