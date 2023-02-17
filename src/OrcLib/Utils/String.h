//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <string_view>

namespace Orc {

bool StartsWith(std::string_view string, std::string_view substring);
bool StartsWith(std::wstring_view string, std::wstring_view substring);

bool EndsWith(std::string_view string, std::string_view substring);
bool EndsWith(std::wstring_view string, std::wstring_view substring);

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
