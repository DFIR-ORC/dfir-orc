//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "stdafx.h"

#include <windows.h>
#include <safeint.h>
#include <psapi.h>

#include "Utils/WinApi.h"
#include "Log/Log.h"

namespace Orc {

std::wstring GetFullPathNameApi(const std::wstring& path, std::error_code& ec) noexcept
{
    try
    {
        DWORD dwLenRequired = GetFullPathNameW(path.c_str(), 0L, NULL, NULL);
        if (dwLenRequired == 0L)
        {
            ec = LastWin32Error();
            Log::Debug(L"Failed GetFullPathNameW buffer length request (path: {}) [{}]", path, ec);
            return {};
        }

        std::wstring fullPath;
        fullPath.resize(dwLenRequired);

        dwLenRequired = GetFullPathNameW(path.c_str(), fullPath.size(), fullPath.data(), NULL);
        if (dwLenRequired == 0)
        {
            ec = LastWin32Error();
            Log::Debug(L"Failed GetFullPathNameW buffer length request (path: {}) [{}]", path, ec);
            return {};
        }

        fullPath.resize(dwLenRequired);
        return fullPath;
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed GetFullPathNameApi '{}' [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "GetFullPathNameApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        return {};
    }
}

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
            Log::Debug(L"Failed ExpandEnvironmentStringsApi '{}' [{}]", szEnvString, ec);
            return {};
        }

        if (cchDone != cchRequired)
        {
            ec.assign(ERROR_INVALID_DATA, std::system_category());
            Log::Debug(L"Failed ExpandEnvironmentStringsApi '{}'", szEnvString);
            return {};
        }

        // remove terminating NULL character
        output.resize(cchDone - 1);
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed ExpandEnvironmentStringsApi '{}' [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "ExpandEnvironmentStringsApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        return {};
    }

    return output;
}

std::wstring ExpandEnvironmentStringsApi(const wchar_t* szEnvString, std::error_code& ec) noexcept
{
    return ExpandEnvironmentStringsApi(szEnvString, ORC_MAX_PATH * sizeof(wchar_t), ec);
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
            Log::Debug("Failed GetWorkingDirectoryApi [{}]", ec);
            return {};
        }

        directory.resize(cch);
        directory.shrink_to_fit();
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed GetWorkingDirectoryApi [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "GetWorkingDirectoryApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        return {};
    }

    return directory;
}

std::wstring GetWorkingDirectoryApi(std::error_code& ec) noexcept
{
    return GetWorkingDirectoryApi(ORC_MAX_PATH * sizeof(wchar_t), ec);
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
            Log::Debug("Failed GetTempPathApi [{}]", ec);
            return {};
        }

        path.resize(cch);
        path.shrink_to_fit();
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed GetTempPathApi [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "GetTempPathApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        return {};
    }

    return path;
}

std::wstring GetTempPathApi(std::error_code& ec) noexcept
{
    return GetTempPathApi(ORC_MAX_PATH * sizeof(wchar_t), ec);
}

std::wstring
GetTempFileNameApi(const wchar_t* lpPathName, const wchar_t* lpPrefixString, UINT uUnique, std::error_code& ec) noexcept
{
    const DWORD cchMaxOutput = ORC_MAX_PATH;
    const DWORD cbMaxOutput = cchMaxOutput * sizeof(wchar_t);

    std::wstring path;
    try
    {
        path.resize(cbMaxOutput);

        size_t cch = ::GetTempFileNameW(lpPathName, lpPrefixString, uUnique, path.data());
        if (cch == 0)
        {
            ec.assign(::GetLastError(), std::system_category());
            Log::Debug("Failed GetTempFileNameApi [{}]", ec);
            return {};
        }

        path.resize(cch);
        path.shrink_to_fit();
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed GetTempFileNameApi [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "GetTempFileNameApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
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
            Log::Debug("Failed GetModuleFileNameApi [{}]", ec);
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
            Log::Debug("Failed GetModuleFileNameApi [{}]", ec);
            return {};
        }

        path.resize(cch);
        path.shrink_to_fit();
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed GetModuleFileNameApi [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "GetTempFileNameApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        return {};
    }

    return path;
}

std::wstring GetModuleFileNameApi(HMODULE hModule, std::error_code& ec) noexcept
{
    return GetModuleFileNameApi(hModule, 32767 * sizeof(wchar_t), ec);
}

std::wstring GetModuleFileNameExApi(HANDLE hProcess, HMODULE hModule, size_t cbMaxOutput, std::error_code& ec) noexcept
{
    std::wstring path;

    try
    {
        path.resize(cbMaxOutput);

        DWORD cch = ::GetModuleFileNameExW(hProcess, hModule, path.data(), path.size());

        DWORD lastError = GetLastError();
        if (cch == 0 || lastError != ERROR_SUCCESS)
        {
            ec.assign(lastError, std::system_category());
            Log::Debug("Failed GetModuleFileNameExApi [{}]", ec);
            return {};
        }

        path.resize(cch);
        path.shrink_to_fit();
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed GetModuleFileNameExApi [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "GetModuleFileNameExApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        return {};
    }

    return path;
}

std::wstring GetModuleFileNameExApi(HANDLE hProcess, HMODULE hModule, std::error_code& ec) noexcept
{
    return GetModuleFileNameExApi(hProcess, hModule, 32767 * sizeof(wchar_t), ec);
}

std::wstring GetComputerNameApi(size_t cbMaxOutput, std::error_code& ec) noexcept
{
    const DWORD cchMaxOutput = static_cast<DWORD>(cbMaxOutput / sizeof(wchar_t));

    std::wstring result;
    try
    {
        result.resize(cbMaxOutput);

        DWORD nSize = cchMaxOutput;
        size_t cch = ::GetComputerNameW(result.data(), &nSize);
        if (cch == 0)
        {
            ec.assign(::GetLastError(), std::system_category());
            Log::Debug("Failed GetComputerNameW [{}]", ec);
            return {};
        }

        result.resize(cch);
        result.shrink_to_fit();
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed GetComputerNameApi [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "GetComputerNameApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        return {};
    }

    return result;
}

std::wstring GetComputerNameApi(std::error_code& ec) noexcept
{
    return GetComputerNameApi(256 * sizeof(wchar_t), ec);
}

std::wstring GetComputerNameExApi(ComputerNameFormat format, size_t cbMaxOutput, std::error_code& ec) noexcept
{
    const DWORD cchMaxOutput = static_cast<DWORD>(cbMaxOutput / sizeof(wchar_t));

    std::wstring result;
    try
    {
        result.resize(cbMaxOutput);

        DWORD nSize = cchMaxOutput;
        size_t cch = ::GetComputerNameW(result.data(), &nSize);
        if (cch == 0)
        {
            ec.assign(::GetLastError(), std::system_category());
            Log::Debug("Failed GetComputerNameW [{}]", ec);
            return {};
        }

        result.resize(cch);
        result.shrink_to_fit();
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed GetComputerNameExApi [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "GetComputerNameExApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        return {};
    }

    return result;
}

std::wstring GetComputerNameExApi(ComputerNameFormat format, std::error_code& ec) noexcept
{
    return GetComputerNameExApi(format, 256 * sizeof(wchar_t), ec);
}

}  // namespace Orc
