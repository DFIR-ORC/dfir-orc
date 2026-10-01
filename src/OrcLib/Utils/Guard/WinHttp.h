//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): DFIR-ORC Team
//

#pragma once

#include "Utils/Guard.h"

#include <winhttp.h>

namespace Orc {
namespace Guard {

// RAII wrapper for a WinHTTP HINTERNET handle (session, connection or request).
class WinHttpHandle final : public DescriptorGuard<HINTERNET>
{
public:
    WinHttpHandle(HINTERNET handle = nullptr)
        : DescriptorGuard<HINTERNET>(handle, nullptr)
    {
    }

    WinHttpHandle(WinHttpHandle&&) noexcept = default;

    WinHttpHandle& operator=(WinHttpHandle&& o) noexcept
    {
        if (this == &o)
        {
            return *this;
        }

        if (m_data != m_invalidValue)
        {
            if (WinHttpCloseHandle(m_data) == FALSE)
            {
                Log::Warn("Failed WinHttpCloseHandle [{}]", LastWin32Error());
            }
        }

        m_data = o.m_data;
        m_invalidValue = o.m_invalidValue;
        o.m_data = m_invalidValue;
        return *this;
    }

    ~WinHttpHandle()
    {
        if (m_data == m_invalidValue)
        {
            return;
        }

        if (WinHttpCloseHandle(m_data) == FALSE)
        {
            Log::Warn("Failed WinHttpCloseHandle [{}]", LastWin32Error());
        }
    }
};

}  // namespace Guard
}  // namespace Orc
