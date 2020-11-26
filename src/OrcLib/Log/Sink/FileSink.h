#pragma once

//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include <memory>
#include <cassert>
#include <fstream>
#include <iostream>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "Log/Sink/MemorySink.h"

//
// FileSink will cache some logs into a MemorySink until the log file is opened
//

namespace Orc {

template <typename Mutex>
class FileSink : public spdlog::sinks::base_sink<Mutex>
{
public:
    using SpdlogFileSink = spdlog::sinks::basic_file_sink<Mutex>;
    using MemorySink = MemorySink<std::vector<uint8_t>, Mutex>;

    FileSink()
        : m_fileSink()
        , m_memorySink(std::make_unique<MemorySink>(4096))
        , m_path()
        , m_lazyClose(false)
    {
    }

    void Open(const std::filesystem::path& path, std::error_code& ec)
    {
        // Log file cannot be opened directly from a multithreaded context
        if (IsOpen())
        {
            ec = std::make_error_code(std::errc::device_or_resource_busy);
            return;
        }

        m_path = path;
    }

    bool IsOpen() const { return m_fileSink != nullptr; }

    void Close()
    {
        m_lazyClose = true;

        // Calling flush with lazyclose will close the file sink behind the base_sink mutex
        spdlog::sinks::base_sink<Mutex>::flush();
    }

protected:
    std::unique_ptr<SpdlogFileSink> LazyOpen(
        const std::filesystem::path& path,
        const std::unique_ptr<MemorySink>& memorySink,
        std::error_code& ec) const
    {
        std::filesystem::remove(path);

        if (memorySink)
        {
            try
            {
                std::fstream log;
                log.open(path, std::ios::out | std::ios::binary);

                const auto& in = memorySink->buffer();
                std::ostream_iterator<char> out(log);
                std::copy(std::cbegin(in), std::cend(in), out);
            }
            catch (const std::system_error& e)
            {
                ec = e.code();
                return {};
            }
            catch (...)
            {
                ec = std::make_error_code(std::errc::interrupted);
                return {};
            }
        }

        auto fileSink = std::make_unique<SpdlogFileSink>(path.string());

        // Current sink_it_ will handle filtering
        fileSink->set_level(spdlog::level::trace);
        if (m_formatter)
        {
            fileSink->set_formatter(m_formatter->clone());
        }

        return fileSink;
    }

    void set_pattern_(const std::string& pattern) override
    {
        set_formatter_(std::make_unique<spdlog::pattern_formatter>(pattern));
    }

    void set_formatter_(std::unique_ptr<spdlog::formatter> formatter) override
    {
        m_formatter = std::move(formatter);

        if (m_fileSink)
        {
            m_fileSink->set_formatter(m_formatter->clone());
        }

        if (m_memorySink)
        {
            m_memorySink->set_formatter(m_formatter->clone());
        }
    }

    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        if (m_fileSink)
        {
            m_fileSink->log(msg);
            return;
        }

        if (m_path.empty() == false && m_lazyClose == false)
        {
            // Lazy open file is made here for thread safety
            std::error_code ec;
            auto fileSink = LazyOpen(m_path, m_memorySink, ec);
            if (ec)
            {
                std::cerr << "Failed to open log file: " << m_path << " (" << ec.message() << ")" << std::endl;
            }
            else
            {
                m_fileSink = std::move(fileSink);
                m_memorySink.reset();
                m_fileSink->log(msg);
                return;
            }
        }

        if (m_memorySink)
        {
            m_memorySink->log(msg);
        }
    }

    void flush_() override
    {
        if (m_fileSink)
        {
            m_fileSink->flush();

            // Lazy reset sink is made here for thread safety
            if (m_lazyClose)
            {
                m_fileSink.reset();
            }
        }
        else
        {
            m_memorySink->flush();
        }
    }

private:
    std::unique_ptr<SpdlogFileSink> m_fileSink;
    std::unique_ptr<MemorySink> m_memorySink;
    std::filesystem::path m_path;
    std::atomic_bool m_lazyClose;
    std::unique_ptr<spdlog::formatter> m_formatter;
};

}  // namespace Orc
