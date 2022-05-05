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

//
// BasicBufferView
//

using BufferView = BasicBufferView<uint8_t>;

inline BasicBufferView<char> ToBasicBufferView(const char* szBuffer)
{
    return BasicBufferView<char>(szBuffer, strlen(szBuffer));
}

inline BasicBufferView<wchar_t> ToBasicBufferView(const wchar_t* szBuffer)
{
    return BasicBufferView<wchar_t>(szBuffer, wcslen(szBuffer));
}

template <typename T>
inline BasicBufferView<typename T::value_type> ToBasicBufferView(const T& buffer)
{
    return BasicBufferView<typename T::value_type>(buffer.data(), buffer.size());
}

//
// BufferView
//

inline BufferView ToBufferView(const char* buffer)
{
    return BufferView(reinterpret_cast<const uint8_t*>(buffer), strlen(buffer));
}

inline BufferView ToBufferView(const wchar_t* buffer)
{
    return BufferView(reinterpret_cast<const uint8_t*>(buffer), wcslen(buffer) * sizeof(wchar_t));
}

template <typename T>
inline BufferView ToBufferView(const T& buffer)
{
    return BufferView(reinterpret_cast<const uint8_t*>(buffer.data()), buffer.size() * sizeof(T::value_type));
}

template <typename T = uint8_t, typename ContainerT>
inline BasicBufferView<T> MakeSubView(const ContainerT& container, uint64_t offset, std::error_code& ec)
{
    if (container.size() < offset)
    {
        ec = std::make_error_code(std::errc::result_out_of_range);
        return {};
    }

    return BasicBufferView<T>(const_cast<T*>(container.data()) + offset, container.size() - offset);
}

template <typename T = uint8_t, typename ContainerT>
inline BasicBufferView<T> MakeSubView(const ContainerT& container, uint64_t offset, size_t count, std::error_code& ec)
{
    if (container.size() < offset || container.size() - offset < count * sizeof(T))
    {
        ec = std::make_error_code(std::errc::result_out_of_range);
        return {};
    }

    return BasicBufferView<T>(reinterpret_cast<const T*>(container.data() + offset), count);
}

}  // namespace Orc
