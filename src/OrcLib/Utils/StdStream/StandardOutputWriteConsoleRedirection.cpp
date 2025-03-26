//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#include "StandardOutputWriteConsoleRedirection.h"

#include <streambuf>
#include <Windows.h>

#include "Utils/TypeTraits.h"

namespace {

template <typename CharT>
class ConsoleStreambuf : public std::basic_streambuf<CharT>
{
public:
    using int_type = typename std::basic_streambuf<CharT>::int_type;
    using CharTraits = std::char_traits<CharT>;

    ConsoleStreambuf()
        : m_hOutput(::GetStdHandle(STD_OUTPUT_HANDLE))
    {
    }

protected:
    int_type overflow(int_type c) override
    {
        m_buffer.push_back(c);

        if (c == Orc::Traits::newline_v<CharT>)
        {
            if (!Flush())
            {
                assert(false && "Failed flush with WriteConsole");
                return CharTraits::to_int_type(CharTraits::eof());
            }
        }

        return c;
    }

    std::streamsize xsputn(const CharT* s, std::streamsize count) override
    {
        BOOL ret = FALSE;
        DWORD written;

        if constexpr (std::is_same_v<CharT, char>)
        {
            ret = ::WriteConsoleA(m_hOutput, s, count, &written, nullptr);
        }
        else
        {
            ret = ::WriteConsoleW(m_hOutput, s, count, &written, nullptr);
        }

        return ret ? count : 0;
    }

    int sync() override
    {
        if (!Flush())
        {
            assert(false && "Failed to sync using WriteConsole");
            return CharTraits::to_int_type(CharTraits::eof());
        }

        return 0;
    }

    BOOL Flush()
    {
        // Call xsputn overrided method
        const auto count = std::basic_streambuf<CharT>::sputn(m_buffer.data(), m_buffer.size());
        BOOL ret = (count == m_buffer.size());
        m_buffer.clear();
        return ret;
    }

private:
    HANDLE m_hOutput;
    std::basic_string<CharT> m_buffer;
};

}  // namespace

namespace Orc {

StandardOutputWriteConsoleRedirection::StandardOutputWriteConsoleRedirection()
    : m_streambuf(std::make_unique<::ConsoleStreambuf<char>>())
    , m_wstreambuf(std::make_unique<::ConsoleStreambuf<wchar_t>>())
    , m_cout(m_streambuf.get())
    , m_redirector(m_cout)
    , m_wcout(m_wstreambuf.get())
    , m_wredirector(m_wcout)
{
}

StandardOutputWriteConsoleRedirection::~StandardOutputWriteConsoleRedirection()
{
    Disable();
}

void StandardOutputWriteConsoleRedirection::Enable()
{
    Log::Debug("Enable stdout redirection with WriteConsole");

    m_redirector.Take(std::cout);
    m_redirector.Take(std::cerr);
    m_wredirector.Take(std::wcout);
    m_wredirector.Take(std::wcerr);
}

void StandardOutputWriteConsoleRedirection::Disable()
{
    m_redirector.Reset();
    m_wredirector.Reset();
}

void StandardOutputWriteConsoleRedirection::Flush(std::error_code& ec)
{
    try
    {
        m_cout.flush();
        m_wcout.flush();
    }
    catch (const std::system_error& e)
    {
        ec = e.code();
        return;
    }
    catch (...)
    {
        ec = std::make_error_code(std::errc::interrupted);
        return;
    }
}

}  // namespace Orc
