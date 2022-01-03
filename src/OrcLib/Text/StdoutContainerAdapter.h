//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <string>

namespace Orc {

// Adapter for a container interface to be usable with Text::Tree and fmt.
// NOTE: log duplication could be done by redirecting stdout using existing StandardOutputRedirection classes
template <typename T>
struct StdoutContainerAdapter
{
public:
    using value_type = T;

    void flush()
    {
        Traits::get_std_out<T>() << m_buffer;
        m_buffer.clear();
        Traits::get_std_out<T>().flush();
    }

    void push_back(T c)
    {
        if (c == Traits::newline_v<T>)
        {
            Log::Info(Log::Facility::kLogFile, m_buffer);

            m_buffer.push_back(c);
            Traits::get_std_out<T>() << m_buffer;
            m_buffer.clear();
        }
        else if (c)
        {
            m_buffer.push_back(c);
        }
    }

private:
    std::basic_string<T> m_buffer;
};

}  // namespace Orc
