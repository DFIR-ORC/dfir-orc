//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "WideStreambufConverter.h"

namespace {

inline bool IsSurrogate(wchar_t cp)
{
    return (cp >= 0xD800 && cp <= 0xDFFF);
}

}  // namespace

namespace Orc {
namespace Command {

WideStreambufConverter::WideStreambufConverter(std::basic_streambuf<char>* output)
    : m_output(output)
{
}

WideStreambufConverter::int_type WideStreambufConverter::overflow(int_type c)
{
    constexpr auto eof = std::char_traits<wchar_t>::to_int_type(std::char_traits<wchar_t>::eof());

    int r1 = 0, r2 = 0, r3 = 0;

    if (::IsSurrogate(c))
    {
        return c;
    }

    if (m_output == nullptr)
    {
        return eof;
    }

    if (c < 0x80)  // ascii and utf-8 overlap range
    {
        r1 = m_output->sputc(static_cast<char>(c));
    }
    else if (c < 0x800)
    {
        r1 = m_output->sputc(static_cast<char>((c >> 6) | 0xC0));
        r2 = m_output->sputc(static_cast<char>((c & 0x3F) | 0x80));
    }
    else  // if (c < 0x10000) ...   This would continue with char32_t...
    {
        r1 = m_output->sputc(static_cast<char>((c >> 12) | 0xE0));
        r2 = m_output->sputc(static_cast<char>(((c >> 6) & 0x3F) | 0x80));
        r3 = m_output->sputc(static_cast<char>((c & 0x3F) | 0x80));
    }

    if (r1 == eof || r2 == eof || r3 == eof)
    {
        return eof;
    }

    return c;
}

int WideStreambufConverter::sync()
{
    if (m_output == nullptr)
    {
        return 0;
    }

    return m_output->pubsync();
}

}  // namespace Command
}  // namespace Orc
