//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <string>

#include "Text/Iconv.h"

namespace Orc {

// Adapter for a container interface to be usable with Text::Tree and fmt.
// NOTE: log duplication could be done by redirecting stdout using existing StandardOutputRedirection classes
template <typename T>
struct StdoutContainerAdapter
{
public:
    using value_type = T;

    StdoutContainerAdapter()
        : m_hStdout(GetStdHandle(STD_OUTPUT_HANDLE))
    {
    }

    void flush()
    {
        if (m_buffer.size())
        {
            DWORD written = 0;
            auto buffer = ToUtf8(m_buffer);
            WriteFile(m_hStdout, (void*)buffer.data(), static_cast<DWORD>(buffer.size()), &written, NULL);
        }

        FlushFileBuffers(m_hStdout);
    }

    void push_back(T c)
    {
        if (c == Traits::newline_v<T>)
        {
            Log::Info(Log::Facility::kLogFile, m_buffer);

            m_buffer.push_back(c);

            DWORD written = 0;
            auto buffer = ToUtf8(m_buffer);
            WriteFile(m_hStdout, (void*)buffer.data(), static_cast<DWORD>(buffer.size()), &written, NULL);
            m_buffer.clear();
        }
        else if (c)
        {
            m_buffer.push_back(c);
        }
    }

private:
    HANDLE m_hStdout;
    std::basic_string<T> m_buffer;
};

}  // namespace Orc
