//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <streambuf>

namespace Orc {

template <typename CharT>
class Tee : public std::basic_streambuf<CharT>
{
public:
    using int_type = typename std::basic_streambuf<CharT>::int_type;

    Tee(std::basic_streambuf<CharT>* s1, std::basic_streambuf<CharT>* s2)
        : m_s1(s1)
        , m_s2(s2)
    {
    }

protected:
    int_type overflow(int_type c) override
    {
        using Traits = std::char_traits<CharT>;

        if (c == Traits::to_int_type(Traits::eof()))
        {
            return c;
        }

        const auto r1 = m_s1->sputc(c);
        const auto r2 = m_s2->sputc(c);
        if (r1 == Traits::to_int_type(Traits::eof()) || r2 == Traits::to_int_type(Traits::eof()))
        {
            return Traits::to_int_type(Traits::eof());
        }

        return c;
    }

    std::streamsize xsputn(const CharT* s, std::streamsize count) override
    {
        auto count1 = m_s1->sputn(s, count);
        auto count2 = m_s2->sputn(s, count);
        assert(count1 == count2);
        return count1 == count2 ? count1 : 0;
    }

    int sync() override
    {
        const auto r1 = m_s1->pubsync();
        const auto r2 = m_s2->pubsync();
        if (r1 != 0 || r2 != 0)
        {
            return -1;
        }

        return 0;
    }

private:
    std::basic_streambuf<CharT>* m_s1;
    std::basic_streambuf<CharT>* m_s2;
};

extern template class Tee<char>;
extern template class Tee<wchar_t>;

}  // namespace Orc
