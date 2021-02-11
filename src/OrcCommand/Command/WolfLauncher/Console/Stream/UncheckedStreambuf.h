//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <streambuf>

namespace Orc {
namespace Command {

template <typename CharT>
class UncheckedStreambuf : public std::basic_streambuf<CharT>
{
public:
    using int_type = typename std::basic_streambuf<CharT>::int_type;

    explicit UncheckedStreambuf(std::basic_streambuf<CharT>* output)
        : m_output(output)
    {
    }

protected:
    int_type overflow(int_type c) override
    {
        if (m_output)
        {
            m_output->sputc(c);
        }

        return c;
    }

    int sync() override
    {
        if (m_output)
        {
            m_output->pubsync();
        }

        return 0;
    }

private:
    std::basic_streambuf<CharT>* m_output;
};

extern template class UncheckedStreambuf<char>;
extern template class UncheckedStreambuf<wchar_t>;

}  // namespace Command
}  // namespace Orc
