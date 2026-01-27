//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <system_error>

#include <windows.h>

[[nodiscard]] inline std::error_code LastWin32Error()
{
    std::error_code ec;
    ec.assign(::GetLastError(), std::system_category());
    return ec;
}
