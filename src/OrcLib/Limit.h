//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <functional>

#include "Utils/TypeTraits.h"

namespace Orc {

template <typename T>
struct Limit;

template <typename T>
struct Limit
{
    Limit(T quantity)
        : value(quantity)
    {
    }
    operator T&() { return value; }
    operator T() const { return value; }
    bool IsUnlimited() const { return value == INFINITE; }
    T value;
};

template <typename T>
struct Limit<Traits::ByteQuantity<T>>
{
    Limit(T quantity)
        : value(quantity)
    {
    }
    operator T&() { return value.value; }
    operator T() const { return value; }
    bool IsUnlimited() const { return value.value == INFINITE; }
    Traits::ByteQuantity<T> value;
};

}  // namespace Orc
