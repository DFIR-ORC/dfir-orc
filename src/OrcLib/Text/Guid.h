//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
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
#include "Utils/BufferView.h"
#include "Utils/Result.h"
#include "Utils/TypeTraits.h"

namespace Orc {

// Example: 3808876B-C176-4E48-B7AE-04046E6CC752
// extern const int kGuidStringLength;
const auto kGuidStringLength = sizeof(GUID) * 2 + 4;

void ToGuid(std::string_view input, GUID& guid, std::error_code& ec);
void ToGuid(std::wstring_view input, GUID& guid, std::error_code& ec);

std::string ToString(const GUID& guid);
std::wstring ToStringW(const GUID& guid);

template <typename OutputIt>
void ToString(const GUID& guid, OutputIt out)
{
    using namespace Orc::Text;
    using value_type = typename Traits::underlying_char_type_t<OutputIt>;

    *out++ = '{';

    if constexpr (std::is_same_v<value_type, char>)
    {
        fmt::format_to(out, "{:08X}-{:04X}-{:04X}-", guid.Data1, guid.Data2, guid.Data3);
    }
    else if constexpr (std::is_same_v<value_type, wchar_t>)
    {
        fmt::format_to(out, L"{:08X}-{:04X}-{:04X}-", guid.Data1, guid.Data2, guid.Data3);
    }
    else
    {
        static_assert("Invalid CharT");
    }

    const auto data4leftLength = 2;
    BufferView data4Left(reinterpret_cast<const uint8_t*>(&guid.Data4[0]), data4leftLength);
    ToHex(std::cbegin(data4Left), std::cend(data4Left), out);

    *out++ = '-';

    BufferView data4right(
        reinterpret_cast<const uint8_t*>(&guid.Data4[data4leftLength]), sizeof(guid.Data4) - data4leftLength);
    ToHex(std::cbegin(data4right), std::cend(data4right), out);

    *out++ = '}';
}

// --- constexpr implementation of ToGuid ---

constexpr uint8_t HexToU8(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    else if (ch >= 'a' && ch <= 'f')
    {
        return 10 + (ch - 'a');
    }
    else if (ch >= 'A' && ch <= 'F')
    {
        return 10 + (ch - 'A');
    }
    else
    {
        static_assert("Invalid hex character");
    }
}

constexpr uint8_t ParseHexByte(char high, char low)
{
    return (HexToU8(high) << 4) | HexToU8(low);
}

constexpr unsigned long ParseHexU32(std::string_view str)
{
    return (HexToU8(str[0]) << 28) | (HexToU8(str[1]) << 24) | (HexToU8(str[2]) << 20) | (HexToU8(str[3]) << 16)
        | (HexToU8(str[4]) << 12) | (HexToU8(str[5]) << 8) | (HexToU8(str[6]) << 4) | HexToU8(str[7]);
}

constexpr unsigned short ParseHexU16(std::string_view str)
{
    return (HexToU8(str[0]) << 12) | (HexToU8(str[1]) << 8) | (HexToU8(str[2]) << 4) | HexToU8(str[3]);
}

constexpr GUID ToGuid(std::string_view str)
{
    if (str.size() != 36 || str[8] != '-' || str[13] != '-' || str[18] != '-' || str[23] != '-')
    {
        static_assert("Invalid GUID format");
    }

    GUID guid {};
    guid.Data1 = ParseHexU32(str.substr(0, 8));
    guid.Data2 = ParseHexU16(str.substr(9, 4));
    guid.Data3 = ParseHexU16(str.substr(14, 4));
    guid.Data4[0] = ParseHexByte(str[19], str[20]);
    guid.Data4[1] = ParseHexByte(str[21], str[22]);
    guid.Data4[2] = ParseHexByte(str[24], str[25]);
    guid.Data4[3] = ParseHexByte(str[26], str[27]);
    guid.Data4[4] = ParseHexByte(str[28], str[29]);
    guid.Data4[5] = ParseHexByte(str[30], str[31]);
    guid.Data4[6] = ParseHexByte(str[32], str[33]);
    guid.Data4[7] = ParseHexByte(str[34], str[35]);

    return guid;
}

}  // namespace Orc
