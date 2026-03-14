//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

//
// Wrappers on fmt to add some flexibility on encoding conversions and formatting
//

#pragma once

#include <type_traits>

#include <fmt/format.h>
#include <fmt/xchar.h>

#include "Text/Iconv.h"
#include "Utils/TypeTraits.h"
#include "Utils/BufferView.h"

namespace Orc {
namespace Text {

namespace details {

template <class...>
constexpr std::false_type always_false {};

template <typename T, typename OutputIt>
void ToUtf16(const T& utf8, OutputIt out)
{
    std::error_code ec;
    auto utf16 = Orc::ToUtf16(MakeStringView(utf8), ec);
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
void ToUtf8(const T& utf16, OutputIt out)
{
    std::error_code ec;
    auto utf8 = Orc::ToUtf8(MakeWStringView(utf16), ec);
    if (ec)
    {
        using namespace std::literals;

        static constexpr auto failed = "<failed to convert to utf8>"sv;
        std::copy(std::cbegin(failed), std::cend(failed), out);
        return;
    }

    std::copy(std::cbegin(utf8), std::cend(utf8), out);
}

template <typename CharT>
constexpr fmt::basic_string_view<CharT> ToFormatView(fmt::basic_string_view<CharT> sv) noexcept
{
    return sv;
}

template <typename CharT>
constexpr fmt::basic_string_view<CharT> ToFormatView(const fmt::runtime_format_string<CharT>& rs) noexcept
{
    return rs.str;
}

template <typename CharT, typename... Args>
constexpr fmt::basic_string_view<CharT> ToFormatView(const fmt::basic_format_string<CharT, Args...>& fs) noexcept
{
    return static_cast<fmt::basic_string_view<CharT>>(fs);
}

template <typename CharT>
constexpr fmt::basic_string_view<CharT> ToFormatView(const std::basic_string<CharT>& s) noexcept
{
    return s;
}

template <typename CharT>
constexpr fmt::basic_string_view<CharT> ToFormatView(const CharT* s) noexcept
{
    return s;
}

template <typename OutputIt, typename FmtArg0, typename... FmtArgs>
inline void FormatWithEncodingTo(OutputIt out, FmtArg0&& arg0, FmtArgs&&... args)
{
    using BufferCharT =
        typename Traits::underlying_char_type<std::remove_cv_t<std::remove_reference_t<OutputIt>>>::type;

    const auto fmtView = details::ToFormatView(arg0);
    using FmtCharT = typename decltype(fmtView)::value_type;

    if constexpr (std::is_same_v<FmtCharT, BufferCharT>)
    {
        if constexpr (std::is_same_v<FmtCharT, char>)
        {
            fmt::vformat_to(out, fmtView, fmt::make_format_args(args...));
        }
        else
        {
            fmt::vformat_to(out, fmtView, fmt::make_format_args<fmt::wformat_context>(args...));
        }
    }
    else if constexpr (std::is_same_v<FmtCharT, char>)
    {
        fmt::basic_memory_buffer<char, 32768> utf8;
        fmt::vformat_to(std::back_inserter(utf8), fmtView, fmt::make_format_args(args...));
        details::ToUtf16(utf8, out);
    }
    else if constexpr (std::is_same_v<FmtCharT, wchar_t>)
    {
        fmt::basic_memory_buffer<wchar_t, 32768> utf16;
        fmt::vformat_to(std::back_inserter(utf16), fmtView, fmt::make_format_args<fmt::wformat_context>(args...));
        details::ToUtf8(utf16, out);
    }
    else
    {
        static_assert(details::always_false<OutputIt>, "Unhandled FmtArg0 type");
    }
}

template <typename OutputIt, typename RawArg>
inline void FormatWithEncodingTo(OutputIt out, RawArg&& arg)
{
    using BufferCharT =
        typename Traits::underlying_char_type<std::remove_cv_t<std::remove_reference_t<OutputIt>>>::type;
    using RawCharT = typename Traits::underlying_char_type<std::remove_cv_t<std::remove_reference_t<RawArg>>>::type;

    if constexpr (std::is_same_v<RawCharT, BufferCharT>)
    {
        std::copy(std::cbegin(arg), std::cend(arg), out);
    }
    else if constexpr (std::is_same_v<RawCharT, char>)
    {
        details::ToUtf16(arg, out);
    }
    else if constexpr (std::is_same_v<RawCharT, wchar_t>)
    {
        details::ToUtf8(arg, out);
    }
    else
    {
        static_assert(details::always_false<OutputIt>, "Unhandled arg type");
    }
}

}  // namespace details

// Wrapper on fmt::format_to which provides:
//   - conversion of FmtArgs arguments to FmtArg0 encoding
//   - conversion of fmt::format_to output to container's encoding
template <typename OutputIt, typename FmtArg0, typename... FmtArgs>
void FormatToWithoutEOL(OutputIt out, FmtArg0&& arg0, FmtArgs&&... args)
{
    try
    {
        using FmtCharT =
            typename Traits::underlying_char_type<std::remove_cv_t<std::remove_reference_t<FmtArg0>>>::type;
        details::FormatWithEncodingTo(out, arg0, TryEncodeTo<FmtCharT>(args)...);
    }
    catch (const fmt::format_error& e)
    {
        assert(nullptr && "Formatting error");
        std::cerr << "Failed to format: " << e.what() << std::endl;

#ifdef DEBUG
        throw;
#endif  // DEBUG
    }
}

// Wrapper on fmt::format_to which provides:
//   - EOL character
//   - conversion of FmtArgs arguments to FmtArg0 encoding
//   - conversion of fmt::format_to output to container's encoding
template <typename OutputIt, typename FmtArg0, typename... FmtArgs>
void FormatTo(OutputIt out, FmtArg0&& arg0, FmtArgs&&... args)
{
    // Use ::type directly to avoid MSVC rejecting _t alias in dependent template context
    using OutCharT = typename Traits::underlying_char_type<std::remove_cv_t<std::remove_reference_t<OutputIt>>>::type;
    FormatToWithoutEOL(out, arg0, std::forward<FmtArgs>(args)...);
    out++ = Traits::newline_v<OutCharT>;
}

}  // namespace Text
}  // namespace Orc
