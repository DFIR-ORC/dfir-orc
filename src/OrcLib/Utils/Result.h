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

namespace Orc {

template <typename T>
using Result = boost::outcome_v2::std_result<T>;

template <typename T>
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

inline std::error_code SystemError(HRESULT hr)
{
    return {hr, std::system_category()};
}

inline std::error_code Win32Error(DWORD win32Error)
{
    return {HRESULT_FROM_WIN32(win32Error), std::system_category()};
}

// Return std::error_code from GetLastError() value
inline std::error_code LastWin32Error()
{
    return {HRESULT_FROM_WIN32(::GetLastError()), std::system_category()};
}

inline HRESULT ToHRESULT(const std::error_code& ec)
{
    assert(ec.category() == std::system_category());
    auto hr = ec.value();
    if ((hr & 0xF0000000) == 0x80000000)
    {
        return hr;
    }

    return HRESULT_FROM_WIN32(hr);
}

}  // namespace Orc
