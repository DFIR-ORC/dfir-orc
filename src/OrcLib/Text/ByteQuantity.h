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
    constexpr std::array<std::string_view, 5> kUnits = {"B", "KB", "MB", "GB", "TB"};

    const auto unitSize = static_cast<T>(base == ByteQuantityBase::Base10 ? 1000 : 1024);

    T value = quantity.value;
    size_t unitIndex = 0;

    // Advance to the largest unit keeping value >= 1.
    // Stops at kUnits.size()-1 so "TB" is the ceiling.
    // value==0 exits immediately at index 0 ("B"), which is correct.
    for (; unitIndex < kUnits.size() - 1; ++unitIndex)
    {
        if (value < unitSize)
            break;

        value /= unitSize;
    }

    fmt::format_to(out, "{} ", value);  // narrow literal: fmt deduces from OutputIt

    const std::string_view unit = kUnits[unitIndex];
    std::copy(std::cbegin(unit), std::cend(unit), out);
}

template <typename OutputIt, typename T>
void ToWString(OutputIt out, const Orc::ByteQuantity<T>& quantity, ByteQuantityBase base = ByteQuantityBase::Base2)
{
    ToString(out, quantity, base);
}

Orc::Result<Orc::ByteQuantity<uint64_t>>
ByteQuantityFromString(std::string_view view, ByteQuantityBase base = ByteQuantityBase::Base2);

Orc::Result<Orc::ByteQuantity<uint64_t>>
ByteQuantityFromString(std::wstring_view view, ByteQuantityBase base = ByteQuantityBase::Base2);

}  // namespace Text
}  // namespace Orc
