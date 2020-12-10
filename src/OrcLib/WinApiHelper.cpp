//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "stdafx.h"

#include <windows.h>
#include <safeint.h>

#include "WinApiHelper.h"

namespace Orc {

std::wstring ExpandEnvironmentStringsApi(const wchar_t* szEnvString, size_t cbMaxOutput, std::error_code& ec) noexcept
{
    const DWORD cchMaxOutput = static_cast<DWORD>(cbMaxOutput / sizeof(wchar_t));
    const DWORD cchRequired = ::ExpandEnvironmentStringsW(szEnvString, NULL, 0L);
    if (cchRequired > cchMaxOutput)
    {
        ec.assign(ERROR_INSUFFICIENT_BUFFER, std::system_category());
        return {};
    }

    std::wstring output;
    try
    {
        output.resize(cchRequired);

        const DWORD cchDone = ::ExpandEnvironmentStringsW(szEnvString, output.data(), cchRequired);
        if (cchDone == 0)
        {
            ec.assign(GetLastError(), std::system_category());
            return {};
        }

        if (cchDone != cchRequired)
        {
            ec.assign(ERROR_INVALID_DATA, std::system_category());
            return {};
        }

        // remove terminating NULL character
        output.resize(cchDone - 1);
    }
    catch (const std::length_error&)
    {
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }

    return output;
}

std::wstring ExpandEnvironmentStringsApi(const wchar_t* szEnvString, std::error_code& ec) noexcept
{
    return ExpandEnvironmentStringsApi(szEnvString, MAX_PATH * sizeof(wchar_t), ec);
}

std::wstring GetWorkingDirectoryApi(size_t cbMaxOutput, std::error_code& ec) noexcept
{
    const DWORD cchMaxOutput = static_cast<DWORD>(cbMaxOutput / sizeof(wchar_t));

    std::wstring directory;
    try
    {
        directory.resize(cbMaxOutput);

        size_t cch = ::GetCurrentDirectoryW(cchMaxOutput, directory.data());
        if (cch == 0)
        {
            ec.assign(::GetLastError(), std::system_category());
            return {};
        }

        directory.resize(cch);
        directory.shrink_to_fit();
    }
    catch (const std::length_error&)
    {
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }

    return directory;
}

std::wstring GetWorkingDirectoryApi(std::error_code& ec) noexcept
{
    return GetWorkingDirectoryApi(MAX_PATH * sizeof(wchar_t), ec);
}

std::wstring GetTempPathApi(size_t cbMaxOutput, std::error_code& ec) noexcept
{
    const DWORD cchMaxOutput = static_cast<DWORD>(cbMaxOutput / sizeof(wchar_t));

    std::wstring path;
    try
    {
        path.resize(cbMaxOutput);

        size_t cch = ::GetTempPathW(cchMaxOutput, path.data());
        if (cch == 0)
        {
            ec.assign(::GetLastError(), std::system_category());
            return {};
        }

        path.resize(cch);
        path.shrink_to_fit();
    }
    catch (const std::length_error&)
    {
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }

    return path;
}

std::wstring GetTempPathApi(std::error_code& ec) noexcept
{
    return GetTempPathApi(MAX_PATH * sizeof(wchar_t), ec);
}

std::wstring
GetTempFileNameApi(const wchar_t* lpPathName, const wchar_t* lpPrefixString, UINT uUnique, std::error_code& ec) noexcept
{
    const DWORD cchMaxOutput = MAX_PATH;
    const DWORD cbMaxOutput = cchMaxOutput * sizeof(wchar_t);

    std::wstring path;
    try
    {
        path.resize(cbMaxOutput);

        size_t cch = ::GetTempFileNameW(lpPathName, lpPrefixString, uUnique, path.data());
        if (cch == 0)
        {
            ec.assign(::GetLastError(), std::system_category());
            return {};
        }

        path.resize(cch);
        path.shrink_to_fit();
    }
    catch (const std::length_error&)
    {
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }

    return path;
}

std::wstring GetTempFileNameApi(const wchar_t* lpPathName, std::error_code& ec) noexcept
{
    return GetTempFileNameApi(lpPathName, nullptr, 0, ec);
}

std::wstring GetModuleFileNameApi(HMODULE hModule, size_t cbMaxOutput, std::error_code& ec) noexcept
{
    std::wstring path;

    try
    {
        path.resize(cbMaxOutput);

        DWORD cch = ::GetModuleFileNameW(hModule, path.data(), path.size());

        DWORD lastError = GetLastError();
        if (lastError != ERROR_SUCCESS)
        {
            ec.assign(lastError, std::system_category());
            return {};
        }

        if (cch == path.size())
        {
            // Assume it is an error for compatibility with XP, see MSDN remark:
            //
            // Windows XP: If the buffer is too small to hold the module name, the function returns nSize. The last
            // error code remains ERROR_SUCCESS. If nSize is zero, the return value is zero and the last error code is
            // ERROR_SUCCESS.
            //
            ec.assign(ERROR_INSUFFICIENT_BUFFER, std::system_category());
            return {};
        }

        path.resize(cch);
        path.shrink_to_fit();
    }
    catch (const std::length_error& e)
    {
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }

    return path;
}

std::wstring GetModuleFileNameApi(HMODULE hModule, std::error_code& ec) noexcept
{
    return GetModuleFileNameApi(hModule, 32767 * sizeof(wchar_t), ec);
}

}  // namespace Orc
