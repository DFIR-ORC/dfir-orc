//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <string_view>
#include <system_error>
#include <cassert>

#include "Utils/Result.h"
#include "Utils/BufferSpan.h"

namespace Orc {
namespace Text {

namespace Details {

struct NibbleTable
{
    uint8_t table[255] = {0};

    constexpr NibbleTable()
        : table {}
    {
        table['1'] = 1;
        table['2'] = 2;
        table['3'] = 3;
        table['4'] = 4;
        table['5'] = 5;
        table['6'] = 6;
        table['7'] = 7;
        table['8'] = 8;
        table['9'] = 9;
        table['a'] = table['A'] = 10;
        table['b'] = table['B'] = 11;
        table['c'] = table['C'] = 12;
        table['d'] = table['D'] = 13;
        table['e'] = table['E'] = 14;
        table['f'] = table['F'] = 15;
    }

    constexpr uint8_t operator[](const uint8_t idx) const { return table[idx]; }
};

}

template <typename Output, typename CharT>
Result<Output> FromHexToLittleEndian(std::basic_string_view<CharT> input)
{
    std::error_code ec;
    auto output = FromHexToLittleEndian<Output>(input, ec);
    if (ec)
    {
        return ec;
    }

    return output;
}

template <typename Output, typename CharT>
Output FromHexToLittleEndian(std::basic_string_view<CharT> input, std::error_code& ec)
{
    Output out = {0};

    constexpr struct Details::NibbleTable table;

    // strip leading '0x'
    if (input.size() > 2 && std::char_traits<CharT>::to_int_type(input[1]) == 'x')
    {
        if (std::char_traits<CharT>::to_int_type(input[0]) != '0')
        {
            ec = std::make_error_code(std::errc::invalid_argument);
            return out;
        }

        input = input.substr(2);
    }

    // strip leading '0's
    size_t i = 0;
    for (; i < input.size(); ++i)
    {
        if (std::char_traits<CharT>::to_int_type(input[i]) != '0')
        {
            break;
        }
    }

    if (i)
    {
        input = input.substr(i);
    }

    // handle 0x0000000000000000000...
    if (input.size() == 0)
    {
        return out;
    }

    // check boundaries
    const uint8_t makeEvenModifier = input.size() % 2 == 0 ? 0 : 1;
    const auto requiredOutputSize = (input.size() >> 1) + makeEvenModifier;
    if (sizeof(Output) < requiredOutputSize)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return out;
    }

    // fill 'Output' as little endian
    BufferSpan outBuffer(reinterpret_cast<uint8_t*>(&out), sizeof(Output));
    auto outIt = std::begin(outBuffer);

    auto it = std::rbegin(input);
    auto last = std::rend(input) - makeEvenModifier;
    while (it < last)
    {
        *(outIt++) = table[*(it + 1)] << 4 | table[*it];
        it += 2;
    }

    if (makeEvenModifier)
    {
        *(outIt++) = table[*it];
    }

    return out;
}

template <typename InputIt, typename OutputIt>
OutputIt FromHex(InputIt first, InputIt last, OutputIt out)
{
    constexpr struct NibbleTable::NibbleTable table;

    auto it = first;
    if (std::distance(first, last) % 2 != 0)
    {
        *(out++) = 0x0f & *(it++);
    }

    while (it < last)
    {
        *(out++) = table[*(it++)] << 4 | table[*(it++)];
    }

    return out;
}

template <typename InputIt, typename OutputIt>
OutputIt ToHex(InputIt first, InputIt last, OutputIt out)
{
    const auto hex = std::string_view("0123456789ABCDEF");

    for (auto it = first; it != last; ++it)
    {
        std::string_view input(reinterpret_cast<const char*>(&(*it)), sizeof(typename InputIt::value_type));
        for (int i = input.size() - 1; i >= 0; --i)
        {
            *out++ = hex[(input[i] >> 4) & 0x0F];
            *out++ = hex[input[i] & 0x0F];
        }
    }

    return out;
}

}  // namespace Text
}  // namespace Orc
