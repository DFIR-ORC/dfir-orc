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
    using Char = Orc::Traits::underlying_char_type_t<std::remove_cv_t<std::remove_reference_t<OutputIt>>>;

    constexpr std::array<std::string_view, 5> kNarrow = {"B", "KB", "MB", "GB", "TB"};
    constexpr std::array<std::wstring_view, 5> kWide = {L"B", L"KB", L"MB", L"GB", L"TB"};

    const auto unitSize = static_cast<T>(base == ByteQuantityBase::Base10 ? 1000 : 1024);
    T value = quantity.value;
    size_t unitIndex = 0;

    for (; unitIndex < kNarrow.size() - 1; ++unitIndex)
    {
        if (value < unitSize)
            break;
        value /= unitSize;
    }

    if constexpr (std::is_same_v<Char, wchar_t>)
        fmt::format_to(out, L"{} {}", value, kWide[unitIndex]);
    else
        fmt::format_to(out, "{} {}", value, kNarrow[unitIndex]);
}

template <typename OutputIt, typename T>
inline void ToWString(OutputIt out, const Orc::ByteQuantity<T>& quantity, ByteQuantityBase base = ByteQuantityBase::Base2)
{
    ToString(out, quantity, base);
}

Orc::Result<Orc::ByteQuantity<uint64_t>>
ByteQuantityFromString(std::string_view view, ByteQuantityBase base = ByteQuantityBase::Base2);

Orc::Result<Orc::ByteQuantity<uint64_t>>
ByteQuantityFromString(std::wstring_view view, ByteQuantityBase base = ByteQuantityBase::Base2);

}  // namespace Text
}  // namespace Orc
