//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <type_traits>

namespace Orc {

template <typename T>
inline std::enable_if_t<std::is_unsigned_v<T>, T> RoundUp(T value, size_t multiple)
{
    if (multiple == 0)
    {
        return value;
    }

    const T remainder = value % multiple;
    if (remainder == 0)
    {
        return value;
    }

    return value + multiple - remainder;
}

// Faster version for power of 2 multiples
template <typename T>
constexpr T RoundUpPow2(T value, size_t multiple)
{
    return ((value - 1u) & ~(static_cast<T>(multiple) - 1u)) + multiple;
}

}  // namespace Orc
