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

std::wstring GetWorkingDirectoryApi(size_t cbMaxOutput, std::error_code& ec) noexcept
{
    const DWORD cchMaxOutput = cbMaxOutput / sizeof(wchar_t);

    std::wstring directory;
    try
    {
        directory.resize(cbMaxOutput);

        size_t cch = ::GetCurrentDirectoryW(cchMaxOutput, directory.data());
        if (cch == 0)
        {
            ec.assign(::GetLastError(), std::system_category());
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

}  // namespace Orc
