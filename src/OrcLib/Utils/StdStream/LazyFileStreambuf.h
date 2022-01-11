//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <streambuf>

#include <fstream>
#include <string>

#include "FileDisposition.h"

namespace Orc {

template <typename CharT>
class LazyFileStreambuf : public std::basic_streambuf<CharT>
{
public:
    using int_type = typename std::basic_streambuf<CharT>::int_type;

    LazyFileStreambuf(size_t bufferSize) { m_buffer.reserve(bufferSize); }

    ~LazyFileStreambuf() override
    {
        std::error_code ec;
        Close(ec);
    }

    void Open(const std::filesystem::path& path, FileDisposition disposition, std::error_code& ec)
    {
        if (m_ofstream.is_open())
        {
            ec = std::make_error_code(std::errc::device_or_resource_busy);
            return;
        }

        try
        {
            m_ofstream.open(path.c_str(), ToOpenMode(disposition) | std::ios_base::binary);
            m_ofstream << m_buffer;
            m_ofstream.flush();
            m_buffer.clear();
            m_buffer.resize(16);

            m_path = std::filesystem::absolute(path);
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

    void Close(std::error_code& ec)
    {
        if (!m_ofstream.is_open())
        {
            return;
        }

        try
        {
            m_ofstream.flush();
            m_ofstream.close();
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

    void Flush(std::error_code& ec)
    {
        if (!m_ofstream.is_open())
        {
            return;
        }

        try
        {
            m_ofstream.flush();
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

    const std::optional<std::filesystem::path>& Path() const { return m_path; }

protected:
    int_type overflow(int_type c) override
    {
        using Traits = std::char_traits<CharT>;

        try
        {
            if (m_ofstream.is_open())
            {
                m_ofstream << static_cast<CharT>(c);
            }
            else
            {
                if (m_buffer.size() < m_buffer.capacity())
                {
                    m_buffer.push_back(static_cast<CharT>(c));
                }
            }
        }
        catch (...)
        {
            assert(nullptr);
            return Traits::to_int_type(Traits::eof());
        }

        return c;
    }

    std::streamsize xsputn(const CharT* s, std::streamsize count) override
    {
        std::basic_string_view<CharT> data(s, count);

        try
        {
            if (m_ofstream.is_open())
            {
                m_ofstream << data;
            }
            else if (m_buffer.size() < m_buffer.capacity())
            {
                m_buffer.append(data);
            }
            else
            {
                return 0;
            }
        }
        catch (...)
        {
            assert(nullptr);
            return 0;
        }

        return count;
    }

    int sync() override
    {
        if (m_ofstream.is_open())
        {
            m_ofstream.flush();
        }

        return 0;
    }

private:
    std::basic_string<CharT> m_buffer;
    std::basic_ofstream<CharT> m_ofstream;
    std::optional<std::filesystem::path> m_path;
};

extern template class LazyFileStreambuf<char>;
extern template class LazyFileStreambuf<wchar_t>;

}  // namespace Orc
