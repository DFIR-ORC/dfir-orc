//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace Orc {

// A plain decimal string as a number: std::nullopt when it is empty, carries anything but ASCII digits
// (a sign, a space, a trailing character) or does not fit on 64 bits. Preferred over the wcstoul,
// wcstoull and _wcstoui64 family, which is locale-dependent, saturates silently on overflow, and needs
// errno cleared then reread to tell a saturated value from a legitimate maximum.
std::optional<uint64_t> ParseDecimalDigits(std::wstring_view text);

bool StartsWith(std::string_view string, std::string_view substring);
bool StartsWith(std::wstring_view string, std::wstring_view substring);

bool EndsWith(std::string_view string, std::string_view substring);
bool EndsWith(std::wstring_view string, std::wstring_view substring);

// 'c' lowercased when it is an ASCII letter, unchanged otherwise. No locale, no CRT case table:
// towlower() and _wcsicmp() fold per the thread's LC_CTYPE, which is what makes them the wrong tool
// for a protocol token (a Turkish LC_CTYPE folds U+0130 to 'i', so a name a peer never wrote can be
// made to match one we look for).
constexpr wchar_t AsciiToLower(wchar_t c) noexcept
{
    return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
}

// Case-insensitive over ASCII ONLY, and ordinal. This is the exact rule HTTP field names (RFC 9110)
// and HTML tag and attribute names are compared under, so folding any wider than ASCII would be a bug
// rather than a courtesy. Length-based: unlike _wcsicmp() on a c_str() it does not stop at an embedded
// NUL, and it needs no terminator on either side.
//
// CompareStringOrdinal() would be the Win32 answer, but it is Vista+ and OrcLib still targets XP
// through the v141_xp toolset - hence a hand-rolled fold.
constexpr bool EqualsAsciiCaseInsensitive(std::wstring_view left, std::wstring_view right) noexcept
{
    if (left.size() != right.size())
    {
        return false;
    }

    // A hand loop rather than std::equal: std::equal is not constexpr before C++20, and OrcLib builds
    // as C++17 under the XP toolset.
    for (size_t i = 0; i < left.size(); ++i)
    {
        if (AsciiToLower(left[i]) != AsciiToLower(right[i]))
        {
            return false;
        }
    }

    return true;
}

// Parent path of a FULL path: everything before the last separator, the trailing separator dropped. An
// empty view when there is none (no parent to name). A relative path without a separator keeps nothing,
// not even a drive letter, which is why callers must hand over a full path.
//
// Preferred over _wsplitpath_s(), whose drive and dir buffers are capped at _MAX_DRIVE and _MAX_DIR: a
// parent over 255 characters made it fail with ERANGE, and an NTFS path reaches 32767.
constexpr std::wstring_view ParentPath(std::wstring_view path) noexcept
{
    const size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring_view::npos)
    {
        return {};
    }

    return path.substr(0, separator);
}

// 'text' with spaces and tabs removed from BOTH ends - on a header line, RFC 9110's OWS. An empty view
// when 'text' holds nothing else.
constexpr std::wstring_view TrimSpacesAndTabs(std::wstring_view text) noexcept
{
    const size_t first = text.find_first_not_of(L" \t");
    if (first == std::wstring_view::npos)
    {
        return {};
    }

    return text.substr(first, text.find_last_not_of(L" \t") - first + 1);
}

template <typename InputIt, typename OutputIt, typename CharT>
void Join(InputIt begin, InputIt end, OutputIt outputIt, CharT delimiter)
{
    const auto itemCount = std::distance(begin, end);

    if (begin == end)
    {
        return;
    }

    std::copy(std::cbegin(*begin), std::cend(*begin), outputIt);
    begin++;

    size_t i = 1;
    for (auto it = begin; it != end; ++it)
    {
        *outputIt = delimiter;
        outputIt++;

        const auto& value = *it;
        std::copy(std::cbegin(value), std::cend(value), outputIt);
    }
}

template <typename InputIt, typename CharT>
void Join(InputIt begin, InputIt end, std::basic_string<CharT>& output, CharT delimiter)
{
    const auto itemCount = std::distance(begin, end);

    size_t charCount = 0;
    for (auto it = begin; it != end; ++it)
    {
        charCount += (*it).size();
    }

    if (charCount == 0)
    {
        return;
    }

    output.reserve(charCount + itemCount - 1);
    Join(begin, end, std::back_inserter(output), delimiter);
}

}  // namespace Orc
