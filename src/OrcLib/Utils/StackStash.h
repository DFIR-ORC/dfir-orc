//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2024 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <array>
#include <memory_resource>

namespace Orc {

template <size_t Elements = 512, typename T = std::byte>
class StackStash final : public std::pmr::polymorphic_allocator<T>
{
public:
    StackStash()
        : std::pmr::polymorphic_allocator<T>(&m_resource)
        , m_resource(m_stack.data(), Elements * sizeof(T))
    {
    }

private:
    std::array<T, Elements> m_stack;
    std::pmr::monotonic_buffer_resource m_resource;
};

}  // namespace Orc
