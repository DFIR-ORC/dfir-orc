//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <windows.h>

#include <system_error>
#include <string_view>
#include <type_traits>
#include <algorithm>

#include <fmt/format.h>

#include "Text/Hex.h"
#include "Utils/Result.h"
#include "Utils/TypeTraits.h"

namespace Orc {

// Example: 3808876B-C176-4E48-B7AE-04046E6CC752
const auto kGuidStringLength = sizeof(GUID) * 2 + 4;

template <typename CharT>
void ToGuid(std::basic_string_view<CharT> input, GUID& guid, std::error_code& ec)
{
    if (input.empty())
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return;
    }

    size_t first = 0;
    if (input[0] == '{')
    {
        first = 1;
    }

    char msb = 0;
    bool isByteComplete = false;
    size_t offset = 0;
    std::array<char, sizeof(GUID)> buffer;

    for (size_t i = first; i < input.size(); ++i)
    {
        char nibble;

        if (input[i] == '}' && input[0] == '{')
        {
            break;
        }

        if (offset >= sizeof(GUID))
        {
            ec = std::make_error_code(std::errc::invalid_argument);
            return;
        }

        if (input[i] == '-')
        {
            continue;
        }

        if ('0' <= input[i] && input[i] <= '9')
        {
            nibble = input[i] - '0';
        }
        else if ('a' <= input[i] && input[i] <= 'f')
        {
            nibble = input[i] - 'a' + 10;
        }
        else if ('A' <= input[i] && input[i] <= 'F')
        {
            nibble = input[i] - 'A' + 10;
        }
        else
        {
            ec = std::make_error_code(std::errc::invalid_argument);
            return;
        }

        if (isByteComplete)
        {
            buffer[offset++] = msb + nibble;
        }
        else
        {
            msb = nibble << 4;
        }

        isByteComplete = !isByteComplete;
    }

    std::string_view view(buffer.data(), buffer.size());

    std::reverse_copy(
        std::cbegin(view), std::cbegin(view) + sizeof(guid.Data1), reinterpret_cast<uint8_t*>(&guid.Data1));
    view.remove_prefix(sizeof(guid.Data1));

    std::reverse_copy(
        std::cbegin(view), std::cbegin(view) + sizeof(guid.Data2), reinterpret_cast<uint8_t*>(&guid.Data2));
    view.remove_prefix(sizeof(guid.Data2));

    std::reverse_copy(
        std::cbegin(view), std::cbegin(view) + sizeof(guid.Data3), reinterpret_cast<uint8_t*>(&guid.Data3));
    view.remove_prefix(sizeof(guid.Data3));

    std::copy(std::cbegin(view), std::cend(view), guid.Data4);
}

extern template void ToGuid(std::basic_string_view<char> input, GUID& guid, std::error_code& ec);
extern template void ToGuid(std::basic_string_view<wchar_t> input, GUID& guid, std::error_code& ec);

template <typename OutputIt>
void ToString(const GUID& guid, OutputIt out)
{
    using namespace Orc::Text;
    using value_type = typename Traits::underlying_char_type_t<OutputIt>;

    *out++ = '{';

    if constexpr (std::is_same_v<value_type, char>)
    {
        fmt::format_to(out, "{:X}-{:X}-{:X}-", guid.Data1, guid.Data2, guid.Data3);
    }
    else if constexpr (std::is_same_v<value_type, wchar_t>)
    {
        fmt::format_to(out, L"{:X}-{:X}-{:X}-", guid.Data1, guid.Data2, guid.Data3);
    }
    else
    {
        static_assert("Invalid CharT");
    }

    const auto data4leftLength = 2;
    std::string_view data4Left(reinterpret_cast<const char*>(&guid.Data4[0]), data4leftLength);
    ToHex(std::cbegin(data4Left), std::cend(data4Left), out);

    *out++ = '-';

    std::string_view data4right(
        reinterpret_cast<const char*>(&guid.Data4[data4leftLength]), sizeof(guid.Data4) - data4leftLength);
    ToHex(std::cbegin(data4right), std::cend(data4right), out);

    *out++ = '}';
}

}  // namespace Orc
