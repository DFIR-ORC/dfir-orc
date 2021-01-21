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
#include <filesystem>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "Log/Sink/MemorySink.h"

//
// FileSink will cache some logs into a MemorySink until the log file is opened
//

namespace Orc {
namespace Log {

template <typename Mutex>
class FileSink : public spdlog::sinks::base_sink<Mutex>
{
public:
    // No need of mutexes since FileSink synchronisation will be made on 'Mutex'
    using SpdlogFileSink = spdlog::sinks::basic_file_sink_st;
    using MemorySink = MemorySink<std::vector<uint8_t>, spdlog::details::null_mutex>;

    const size_t kMemorySinkSize = 4096;

    FileSink()
        : m_fileSink()
        , m_memorySink(std::make_unique<MemorySink>(kMemorySinkSize))
    {
    }

    ~FileSink() override { Close(); }

    void Open(const std::filesystem::path& path, std::error_code& ec)
    {
        std::lock_guard<Mutex> lock(mutex_);

        if (m_fileSink != nullptr)
        {
            ec = std::make_error_code(std::errc::device_or_resource_busy);
            return;
        }

        std::filesystem::remove(path);

        // Dump memorySink if any
        if (m_memorySink)
        {
            try
            {
                std::fstream log;
                log.open(path, std::ios::out | std::ios::binary);

                const auto& in = m_memorySink->buffer();
                std::ostream_iterator<char> out(log);
                std::copy(std::cbegin(in), std::cend(in), out);
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

            m_memorySink.reset();
        }

        // use absolute path so spdlog's functions like Filename_t() will also return the same absolute path
        auto absolutePath = std::filesystem::absolute(path, ec);
        if (ec)
        {
            Log::Warn(L"Failed to resolve absolute path: {} [{}]", path, ec);
            absolutePath = path;  // better than empty
            ec.clear();
        }

        m_fileSink = std::make_unique<SpdlogFileSink>(absolutePath.string());

        // Current sink_it_ will handle filtering
        m_fileSink->set_level(spdlog::level::trace);
        if (m_formatter)
        {
            m_fileSink->set_formatter(m_formatter->clone());
        }
    }

    bool IsOpen()
    {
        std::lock_guard<Mutex> lock(mutex_);
        return m_fileSink != nullptr;
    }

    void Close()
    {
        std::lock_guard<Mutex> lock(mutex_);
        flush_();
        m_memorySink = std::make_unique<MemorySink>(kMemorySinkSize);
        m_fileSink.reset();
    }

    std::optional<std::filesystem::path> OutputPath() const
    {
        if (m_fileSink)
        {
            return m_fileSink->filename();
        }

        return {};
    }

protected:
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
        }
    }

private:
    std::unique_ptr<SpdlogFileSink> m_fileSink;
    std::unique_ptr<MemorySink> m_memorySink;
    std::unique_ptr<spdlog::formatter> m_formatter;
};

}  // namespace Log
}  // namespace Orc
