//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string_view>
#include <system_error>

#include <gsl/span>

namespace Orc {

template <typename T>
using BasicBufferView = gsl::span<const T>;

using BufferView = BasicBufferView<uint8_t>;

template <typename T = uint8_t, typename ContainerT>
BasicBufferView<T> MakeSubView(const ContainerT& container, uint64_t offset, std::error_code& ec)
{
    if (container.size() < offset)
    {
        ec = std::make_error_code(std::errc::result_out_of_range);
        return {};
    }

    return BasicBufferView<T>(const_cast<T*>(container.data()) + offset, container.size() - offset);
}

template <typename T = uint8_t, typename ContainerT>
BasicBufferView<T> MakeSubView(const ContainerT& container, uint64_t offset, size_t count, std::error_code& ec)
{
    if (container.size() < offset || container.size() - offset < count * sizeof(T))
    {
        ec = std::make_error_code(std::errc::result_out_of_range);
        return {};
    }

    return BasicBufferView<T>(reinterpret_cast<const T*>(container.data() + offset), count);
}

}  // namespace Orc
