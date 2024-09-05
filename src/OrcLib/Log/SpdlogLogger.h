//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <vector>

#include <spdlog/logger.h>

#include "Log/SpdlogSink.h"

namespace Orc {
namespace Log {

// https://github.com/gabime/spdlog/wiki/3.-Custom-formatting
// The following could output: '2020-09-30T13:43:41.256Z [I] this is a foobar log'
// The %^...%$ options specify coloring range, only one is currently supported
inline constexpr std::string_view kDefaultLogPattern("%^%Y-%m-%dT%T.%eZ [%L] %v%$");

class SpdlogLogger
{
public:
    using Ptr = std::shared_ptr<SpdlogLogger>;

    SpdlogLogger(const std::string& name);

    void Add(SpdlogSink::Ptr sink);

    Log::Level Level() const;
    void SetLevel(Log::Level level);

    void SetPattern(const std::string& pattern);
    void SetFormatter(std::unique_ptr<spdlog::formatter> formatter);

    void EnableBacktrace(size_t messageCount);
    void DisableBacktrace();
    void DumpBacktrace();

    Log::Level BacktraceTrigger() const { return m_backtraceTrigger; }
    void SetBacktraceTrigger(Log::Level level) { m_backtraceTrigger = level; }

    Log::Level BacktraceLevel() const { return m_backtraceLevel; }
    void SetBacktraceLevel(Log::Level level) { m_backtraceLevel = level; }

    void SetErrorHandler(std::function<void(const std::string&)> handler);

    void SetAsDefaultLogger();

    const std::vector<SpdlogSink::Ptr>& Sinks();

    inline void Log(const std::chrono::system_clock::time_point& timepoint, Log::Level level, fmt::string_view msg)
    {
        if (level >= m_backtraceTrigger && m_backtraceTrigger != Level::Off)
        {
            DumpBacktrace();
        }

        m_logger->log(timepoint, spdlog::source_loc {}, static_cast<spdlog::level::level_enum>(level), msg);
    }

    void Log(const std::chrono::system_clock::time_point& timepoint, Log::Level level, fmt::wstring_view msg);

    template <typename... Args>
    inline void Trace(fmt::format_string<Args...> fmt, Args&&... args)
    {
        // Never forward trace level calls to avoid any spdlog's backtrace processing
        if (m_logger->level() > spdlog::level::trace)
        {
            return;
        }

        m_logger->trace(fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    inline void Trace(fmt::wformat_string<Args...> fmt, Args&&... args)
    {
        // Never forward trace level calls to avoid any spdlog's backtrace processing
        if (m_logger->level() > spdlog::level::trace)
        {
            return;
        }

        m_logger->trace(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void Debug(fmt::format_string<Args...> fmt, Args&&... args)
    {
        m_logger->debug(fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    inline void Debug(fmt::wformat_string<Args...> fmt, Args&&... args)
    {
        m_logger->debug(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void Info(fmt::format_string<Args...> fmt, Args&&... args)
    {
        m_logger->info(fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    inline void Info(fmt::wformat_string<Args...> fmt, Args&&... args)
    {
        m_logger->info(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void Warn(fmt::format_string<Args...> fmt, Args&&... args)
    {
        if (m_backtraceTrigger != Level::Off && m_backtraceTrigger >= Level::Warning)
        {
            DumpBacktrace();
        }

        m_logger->warn(fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    inline void Warn(fmt::wformat_string<Args...> fmt, Args&&... args)
    {
        if (m_backtraceTrigger != Level::Off && m_backtraceTrigger >= Level::Warning)
        {
            DumpBacktrace();
        }

        m_logger->warn(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void Error(fmt::format_string<Args...> fmt, Args&&... args)
    {
        if (m_backtraceTrigger != Level::Off && m_backtraceTrigger >= Level::Error)
        {
            DumpBacktrace();
        }

        m_logger->error(fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    inline void Error(fmt::wformat_string<Args...> fmt, Args&&... args)
    {
        if (m_backtraceTrigger != Level::Off && m_backtraceTrigger >= Level::Error)
        {
            DumpBacktrace();
        }

        m_logger->error(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void Critical(fmt::format_string<Args...> fmt, Args&&... args)
    {
        if (m_backtraceTrigger == Level::Critical)
        {
            DumpBacktrace();
        }

        m_logger->critical(fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    inline void Critical(fmt::wformat_string<Args...> fmt, Args&&... args)
    {
        if (m_backtraceTrigger == Level::Critical)
        {
            DumpBacktrace();
        }

        m_logger->critical(fmt, std::forward<Args>(args)...);
    }

    void Flush() { m_logger->flush(); }

private:
    std::shared_ptr<spdlog::logger> m_logger;
    std::vector<SpdlogSink::Ptr> m_sinks;
    std::unique_ptr<spdlog::formatter> m_backtraceFormatter;
    Log::Level m_backtraceTrigger;
    Log::Level m_backtraceLevel;
};

}  // namespace Log
}  // namespace Orc
