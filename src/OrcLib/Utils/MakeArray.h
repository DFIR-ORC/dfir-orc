//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <array>

namespace Orc {

//
// Helper for making std::array without specifying the size
// https://stackoverflow.com/a/26351760
//
template <typename V, typename... T>
constexpr auto MakeArray(T&&... t) -> std::array<V, sizeof...(T)>
{
    return {{std::forward<T>(t)...}};
}

}  // namespace Orc
