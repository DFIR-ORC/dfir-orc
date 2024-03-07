//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <optional>

#include <boost/outcome/outcome.hpp>

#include <winternl.h>

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

template <typename T = void>
using Success = boost::outcome_v2::success_type<T>;

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
