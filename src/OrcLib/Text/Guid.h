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
//extern const int kGuidStringLength;
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
    std::string_view data4Left(reinterpret_cast<const char*>(&guid.Data4[0]), data4leftLength);
    ToHex(std::cbegin(data4Left), std::cend(data4Left), out);

    *out++ = '-';

    std::string_view data4right(
        reinterpret_cast<const char*>(&guid.Data4[data4leftLength]), sizeof(guid.Data4) - data4leftLength);
    ToHex(std::cbegin(data4right), std::cend(data4right), out);

    *out++ = '}';
}

}  // namespace Orc
