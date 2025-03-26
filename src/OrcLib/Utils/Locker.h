//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <mutex>

namespace Orc {

template <typename T>
class Locker
{
public:
    auto Get()
    {
        return std::pair<T&, std::lock_guard<std::mutex>>(
            std::piecewise_construct, std::forward_as_tuple(object), std::forward_as_tuple(m_mutex));
    }

    auto Get() const
    {
        return std::pair<T&, std::lock_guard<std::mutex>>(
            std::piecewise_construct, std::forward_as_tuple(object), std::forward_as_tuple(m_mutex));
    }

private:
    mutable std::mutex m_mutex;
    T object;
};

}  // namespace Orc
