//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2025 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Utils/TypeTraits.h"
#include "Utils/Result.h"

namespace Orc {
namespace Text {

template <typename OutputIt, typename T>
void ToString(OutputIt out, const Orc::ByteQuantity<T>& quantity, ByteQuantityBase base = ByteQuantityBase::Base2)
{
    constexpr std::array units = {
        std::string_view("B"),
        std::string_view("KB"),
        std::string_view("MB"),
        std::string_view("GB"),
        std::string_view("TB")};

    const size_t unitSize = (base == ByteQuantityBase::Base10 ? 1000 : 1024);

    T value = quantity.value;
    size_t index = 0;
    for (; index < units.size() - 1; ++index)
    {
        if (value < unitSize)
        {
            break;
        }

        value = value / unitSize;
    }

    if constexpr (std::is_same_v<T, char>)
    {
        fmt::format_to(out, "{} ", value);
    }
    else
    {
        fmt::format_to(out, L"{} ", value);
    }

    const auto& unit = units[index];
    std::copy(std::cbegin(unit), std::cend(unit), out);
}

Orc::Result<Orc::ByteQuantity<uint64_t>>
ByteQuantityFromString(std::string_view view, ByteQuantityBase base = ByteQuantityBase::Base2);

Orc::Result<Orc::ByteQuantity<uint64_t>>
ByteQuantityFromString(std::wstring_view view, ByteQuantityBase base = ByteQuantityBase::Base2);

}  // namespace Text
}  // namespace Orc
