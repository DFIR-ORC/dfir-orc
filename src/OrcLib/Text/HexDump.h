//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <algorithm>
#include <cctype>
#include <locale>
#include <string_view>

#include "fmt/core.h"

#include "Utils/TypeTraits.h"

namespace Orc {
namespace Text {

namespace Detail {

//
// Hex/Offset column formatting string
//
template <typename T>
auto OffsetFmt()
{
    if constexpr (std::is_same_v<T, char>)
    {
        return std::string_view("{}{:010X}");
    }
    else if (std::is_same_v<T, wchar_t>)
    {
        return std::wstring_view(L"{}{:010X}");
    }
}

template <typename T>
auto HexFmt()
{
    if constexpr (std::is_same_v<T, char>)
    {
        return std::string_view(" {:02X}");
    }
    else if (std::is_same_v<T, wchar_t>)
    {
        return std::wstring_view(L" {:02X}");
    }
}

template <typename OutputIt, typename T>
OutputIt Hex(OutputIt out, T value)
{
    const auto hexAlphabet = std::string_view("0123456789ABCDEF");
    std::string_view in(reinterpret_cast<const char*>(&value), sizeof(value));
    std::array<char, 2 * sizeof(value)> hex;

    for (size_t i = 0; i < in.size(); ++i)
    {
        hex[i * 2 + 1] = hexAlphabet[in[i] & 0x0F];
        hex[i * 2] = hexAlphabet[(in[i] >> 4) & 0x0F];
    }

    return std::copy(std::cbegin(hex), std::cend(hex), out);
}

}  // namespace Detail

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

//
// Push back into 'out' of type U the hex dump of 'in' with indentation 'indent'
//
template <typename T, typename InputIt, typename OutputIt>
void HexDump(const T& indent, InputIt first, InputIt last, OutputIt out)
{
    using namespace Orc::Text::Detail;
    using value_type = typename Traits::underlying_char_type_t<OutputIt>;
    const size_t bytesPerLine = 16;

    // printable column
    std::basic_string<value_type> printable;

    size_t offset = 0;
    for (InputIt in = first; in != last; ++in, ++offset)
    {
        if (offset % bytesPerLine == 0)
        {
            if (offset != 0)
            {
                out++ = Traits::space_v<value_type>;
                std::copy(std::cbegin(printable), std::cend(printable), out);
                printable.clear();
                out++ = Traits::newline_v<value_type>;
            }

            fmt::format_to(out, OffsetFmt<value_type>(), indent, offset);
        }

        out++ = Traits::space_v<value_type>;

        const auto value = *in;
        Hex(out, value);
        printable.push_back(Traits::isprintable(value) ? value : Traits::dot_v<value_type>);
    }

    if (!printable.empty())
    {
        if (offset % bytesPerLine)
        {
            // Need padding for "missing" bytes
            const size_t bytesToPad = bytesPerLine - offset % bytesPerLine;
            const size_t padding = 2 * bytesToPad + bytesToPad;

            for (size_t i = 0; i < padding; ++i)
            {
                out++ = Traits::space_v<value_type>;
            }
        }

        out++ = Traits::space_v<value_type>;
        std::copy(std::cbegin(printable), std::cend(printable), out);
    }

    out++ = Traits::newline_v<value_type>;
}

template <typename InputIt, typename OutputIt>
void HexDump(InputIt first, InputIt last, OutputIt out)
{
    HexDump(GetIndent<typename U::value_type>(0), in, out);
}

}  // namespace Text
}  // namespace Orc
