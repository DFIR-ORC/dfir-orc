//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "String.h"

#include <charconv>
#include <string>

#include <boost/algorithm/string/predicate.hpp>

namespace {

template <typename T>
bool StartsWith(std::basic_string_view<T> string, std::basic_string_view<T> substring)
{
    if (string.size() < substring.size())
    {
        return false;
    }

    return boost::iequals(std::basic_string_view<T>(string.data(), substring.size()), substring);
}

template <typename T>
bool EndsWith(std::basic_string_view<T> string, std::basic_string_view<T> substring)
{
    if (string.size() < substring.size())
    {
        return false;
    }

    return boost::iequals(
        std::basic_string_view<T>(string.data() + (string.size() - substring.size()), substring.size()), substring);
}

}  // namespace

namespace Orc {

std::optional<uint64_t> ParseDecimalDigits(std::wstring_view text)
{
    if (text.empty())
    {
        return std::nullopt;
    }

    // std::from_chars has no wchar_t overload, so the text is narrowed first; every accepted character is
    // an ASCII digit, so that is lossless.
    std::string narrow;
    narrow.reserve(text.size());
    for (const wchar_t c : text)
    {
        if (c < L'0' || c > L'9')
        {
            return std::nullopt;
        }

        narrow.push_back(static_cast<char>(c));
    }

    uint64_t value = 0;
    const std::from_chars_result result = std::from_chars(narrow.data(), narrow.data() + narrow.size(), value);
    if (result.ec != std::errc())
    {
        return std::nullopt;  // out of range; the digit filter above already rules out a malformed number
    }

    return value;
}

bool StartsWith(std::string_view string, std::string_view substring)
{
    return ::StartsWith(std::forward<std::string_view>(string), std::forward<std::string_view>(substring));
}

bool StartsWith(std::wstring_view string, std::wstring_view substring)
{
    return ::StartsWith(std::forward<std::wstring_view>(string), std::forward<std::wstring_view>(substring));
}

bool EndsWith(std::string_view string, std::string_view substring)
{
    return ::EndsWith(std::forward<std::string_view>(string), std::forward<std::string_view>(substring));
}

bool EndsWith(std::wstring_view string, std::wstring_view substring)
{
    return ::EndsWith(std::forward<std::wstring_view>(string), std::forward<std::wstring_view>(substring));
}

}  // namespace Orc
