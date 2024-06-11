//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <optional>
#include <winternl.h>
#include <system_error>
#include <cassert>

#ifdef __cpp_lib_expected
#    include <expected>

namespace Orc {

template <typename T>
concept not_void = !std::is_same_v<T, void>;

template <typename T = void>
struct Result : std::expected<T, std::error_code>
{
    constexpr Result(Result<T>&& value) noexcept = default;
    constexpr Result(const Result<T>& value) noexcept = delete;

    constexpr Result& operator=(Result<T>&& value) noexcept = default;
    constexpr Result& operator=(const Result<T>& value) noexcept = default;

    constexpr Result& operator=(const std::errc& ec)
    {
        return *this = std::unexpected(std::make_error_code(ec));
    }

    constexpr Result& operator=(const std::error_code& ec)
    {
        return *this = std::unexpected(ec);
    }

    template <typename U = T>
    constexpr Result(U&& value) noexcept
        requires(!std::is_same_v<U, void> && std::is_convertible_v<U, T>)
        : std::expected<T, std::error_code>(std::forward<U>(value)) {};

    constexpr Result(std::errc ec)
        : std::expected<T, std::error_code>(std::unexpected(std::make_error_code(ec))) {};

    constexpr Result(std::error_code ec)
        : std::expected<T, std::error_code>(std::unexpected(ec)) {};

    constexpr Result(std::unexpected<std::error_code> unexpected)
        : std::expected<T, std::error_code>(std::move(unexpected)) {};

    template <typename... Args>
    constexpr Result(Args&&... args) noexcept
        requires std::constructible_from<T, Args...>
        : std::expected<T, std::error_code>(std::in_place, args...)
    {
    }

    constexpr inline bool has_error() const { return !this->has_value(); }
};


template <>
struct Result<void> : std::expected<void, std::error_code>
{

    constexpr Result(std::errc ec)
        : std::expected<void, std::error_code>(std::unexpected(std::make_error_code(ec))) {};

    Result(std::error_code ec) noexcept
        : std::expected<void, std::error_code>(std::unexpected(ec)) {};

    Result(std::unexpected<std::error_code> unexpected)
        : std::expected<void, std::error_code>(std::move(unexpected)) {};

    template <typename... Args>
    constexpr Result(Args&&... args)
        : std::expected<void, std::error_code>()
    {
    }

    constexpr inline bool has_error() const { return !this->has_value(); }
};

template <typename T>
inline Result<T> Success(auto... args)
    requires not_void<T> && std::constructible_from<T, decltype(args)...>
{
    return {std::in_place, args...};
}

template <>
inline Result<void> Success()
{
    return {std::in_place};
}

using Fail = std::unexpected<std::error_code>;


inline std::error_code SystemError(HRESULT hr)
{
    return {hr, std::system_category()};
}

inline std::error_code Win32Error(DWORD win32Error)
{
    return {HRESULT_FROM_WIN32(win32Error), std::system_category()};
}

inline std::error_code NtError(NTSTATUS ntStatus)
{
    return std::error_code {HRESULT_FROM_NT(ntStatus), std::system_category()};
}

// Return std::error_code from GetLastError() value
inline std::error_code LastWin32Error()
{
    return std::error_code {HRESULT_FROM_WIN32(::GetLastError()), std::system_category()};
}

}  // namespace Orc

#    define ORC_TRY(value, expr)                                                                                        \
        auto&& _result = expr;                                                                                         \
        if (!_result.has_value())                                                                                      \
            return std::unexpected(_result.error());                                                              \
        auto value = *_result;

#else

#    include <boost/outcome/outcome.hpp>


namespace Orc {

template <typename T>
struct Result : boost::outcome_v2::std_result<T>
{
    template <typename... Args>
    Result(Args&&... args)
        : boost::outcome_v2::std_result<T>(std::forward<Args>(args)...)
    {
    }

    template <class U>
    constexpr auto value_or(U&& defaultValue) const&
    {
        if (boost::outcome_v2::std_result<T>::has_error())
        {
            return T {defaultValue};
        }

        return boost::outcome_v2::std_result<T>::value();
    }

    template <class U>
    constexpr auto value_or(U&& defaultValue) &&
    {
        if (boost::outcome_v2::std_result<T>::has_error())
        {
            return T {defaultValue};
        }

        return boost::outcome_v2::std_result<T>::value();
    }

    constexpr const T* operator->() const { return &boost::outcome_v2::std_result<T>::value(); }
    constexpr T* operator->() { return &boost::outcome_v2::std_result<T>::value(); }
    constexpr const T& operator*() const& { return boost::outcome_v2::std_result<T>::value(); }
    constexpr T& operator*() & { return boost::outcome_v2::std_result<T>::value(); }
    constexpr const T&& operator*() const&& { return std::forward<T>(boost::outcome_v2::std_result<T>::value()); }
    constexpr T&& operator*() && { return std::forward<T>(boost::outcome_v2::std_result<T>::value()); }
};

template <>
struct Result<void> : boost::outcome_v2::std_result<void>
{
    template <typename... Args>
    Result(Args&&... args)
        : boost::outcome_v2::std_result<void>(std::forward<Args>(args)...)
    {
    }
};

template <typename T, typename... Args>
inline Result<T> Success(Args&&... args)
{
    return {boost::outcome_v2::in_place_type<void>, args...};
}

inline std::error_code SystemError(HRESULT hr)
{
    return {hr, std::system_category()};
}

inline std::error_code Win32Error(DWORD win32Error)
{
    return {HRESULT_FROM_WIN32(win32Error), std::system_category()};
}

inline std::error_code NtError(NTSTATUS ntStatus)
{
    return {HRESULT_FROM_NT(ntStatus), std::system_category()};
}

// Return std::error_code from GetLastError() value
inline std::error_code LastWin32Error()
{
    return {HRESULT_FROM_WIN32(::GetLastError()), std::system_category()};
}

} // namespace Orc

#endif

namespace Orc {

template <typename T>
T& operator*(Result<T>& result)
{
    return result.value();
}

template <typename T>
const T& operator*(const Result<T>& result)
{
    return result.value();
}

template <typename T>
std::optional<T> ToOptional(const Orc::Result<T>& result)
{
    if (result.has_value())
    {
        return result.value();
    }

    return {};
}

template <typename T>
std::optional<T> ToOptional(Orc::Result<T>&& result)
{
    if (result.has_value())
    {
        return std::move(result.value());
    }

    return {};
}

template <typename T>
T ValueOr(Orc::Result<T>&& result, const T& fallback_value)
{
    if (result.has_value())
    {
        return std::move(result.value());
    }

    return fallback_value;
}

template <typename T>
T ValueOr(Orc::Result<T>&& result, T&& fallback_value)
{
    if (result.has_value())
    {
        return std::move(result.value());
    }

    return std::move(fallback_value);
}

inline HRESULT ToHRESULT(const std::error_code& ec)
{
    if (ec.category() != std::system_category())
    {
        return ec ? E_FAIL : S_OK;
    }

    auto hr = ec.value();
    if ((hr & 0xF0000000) == 0x80000000)
    {
        return hr;
    }

    return HRESULT_FROM_WIN32(hr);
}

}  // namespace Orc
