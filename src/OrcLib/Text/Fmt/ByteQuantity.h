//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Output/Text/Fmt/Fwd/ByteQuantity.h"

#include <array>
#include <string>

#include "Utils/TypeTraits.h"

template <typename OutputIt, typename T>
void FormatByteQuantityTo(OutputIt out, const Orc::Traits::ByteQuantity<T>& quantity)
{
    constexpr std::array units = {"B", "KB", "MB", "GB", "TB"};
    const size_t step = 1000;

    T value = quantity.value;
    size_t index = 0;
    for (; index < units.size() - 1; ++index)
    {
        if (value < step)
        {
            break;
        }

        value = value / step;
    }

    fmt::format_to(out, "{} {}", value, units[index]);
}

template <typename OutputIt, typename T>
void FormatByteQuantityToW(OutputIt out, const Orc::Traits::ByteQuantity<T>& quantity)
{
    constexpr std::array units = {L"B", L"KB", L"MB", L"GB", L"TB"};
    const size_t step = 1000;

    T value = quantity.value;
    size_t index = 0;
    for (; index < units.size() - 1; ++index)
    {
        if (value < step)
        {
            break;
        }

        value = value / step;
    }

    fmt::format_to(out, L"{} {}", value, units[index]);
}

// TODO: fix
// template <typename OutputIt, typename T>
// void FormatByteQuantityTo2(OutputIt out, const Orc::Traits::ByteQuantity<T>& quantity)
//{
//    constexpr size_t kUnitsLen = 5;
//    const size_t step = 1000;\
//
//    T value = quantity;
//    size_t index = 0;
//    for (; index < kUnitsLen; ++index)
//    {
//        if (value < step)
//        {
//            break;
//        }
//
//        value = value / step;
//    }
//
//    using CharT = Orc::Traits::underlying_char_type_t<OutputIt>;
//    if constexpr (std::is_same_v<CharT, char>)
//    {
//        const std::array<const char*, kUnitsLen> units = {{"B", "KB", "MB", "GB", "TB"}};
//        //fmt::vformat_to<CharT>(out, "{} {}", value, units[index]);
//        fmt::format_to(out, "{} {}", value, units[index]);
//        //fmt::format_to(out, L"{} {}", value, unitsW[index]);
//    }
//    else if constexpr(std::is_same_v<CharT, wchar_t>)
//    {
//        const std::array<const wchar_t*, kUnitsLen> units = {{L"B", L"KB", L"MB", L"GB", L"TB"}};
//        //fmt::vformat_to<CharT>(out, L"{} {}", value, unitsW[index]);
//        fmt::format_to(out, L"{} {}", value, units[index]);
//    }
//    else
//    {
//        //CharT foo = what;
//    }
//}

template <typename T>
template <typename FormatContext>
auto fmt::formatter<Orc::Traits::ByteQuantity<T>>::format(
    const Orc::Traits::ByteQuantity<T>& quantity,
    FormatContext& ctx) -> decltype(ctx.out())
{
    std::string s;
    FormatByteQuantityTo(std::back_inserter(s), quantity);
    return formatter<std::string_view>::format(s, ctx);
}

template <typename T>
template <typename FormatContext>
auto fmt::formatter<Orc::Traits::ByteQuantity<T>, wchar_t>::format(
    const Orc::Traits::ByteQuantity<T>& quantity,
    FormatContext& ctx) -> decltype(ctx.out())
{
    std::wstring s;
    FormatByteQuantityToW(std::back_inserter(s), quantity);
    return formatter<std::wstring_view, wchar_t>::format(s, ctx);
}
