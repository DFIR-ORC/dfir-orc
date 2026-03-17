//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include "Log/SpdlogLogger.h"

#include "Text/Iconv.h"

namespace Orc {
namespace Log {

SpdlogLogger::SpdlogLogger(const std::string& name)
    : m_logger(std::make_shared<spdlog::logger>(name))
    , m_sinks()
    , m_backtraceFormatter(
          std::make_unique<spdlog::pattern_formatter>(std::string(kDefaultLogPattern), spdlog::pattern_time_type::utc))
    , m_backtraceTrigger(Level::Off)
    , m_backtraceLevel(Level::Debug)
{
    m_logger->flush_on(spdlog::level::err);
}

void SpdlogLogger::Add(SpdlogSink::Ptr sink)
{
    sink->AddTo(*m_logger);
    m_sinks.push_back(std::move(sink));
}

Log::Level SpdlogLogger::Level() const
{
    return static_cast<Log::Level>(m_logger->level());
}

void SpdlogLogger::SetLevel(Log::Level level)
{
    m_logger->set_level(static_cast<spdlog::level::level_enum>(level));
}

void SpdlogLogger::EnableBacktrace(size_t messageCount)
{
    m_backtraceSink = std::make_shared<BacktraceSink<std::mutex>>(messageCount);
    m_backtraceSink->set_level(spdlog::level::debug);
    m_logger->sinks().push_back(m_backtraceSink);
}

void SpdlogLogger::DisableBacktrace()
{
    m_logger->disable_backtrace();
}

void SpdlogLogger::DumpBacktrace()
{
    if (m_backtraceSink)
    {
        // This is where the complete pattern formatting finally happens.
        m_backtraceSink->dump_to(*m_logger);
    }
}

void SpdlogLogger::SetErrorHandler(std::function<void(const std::string&)> handler)
{
    m_logger->set_error_handler(handler);
}

void SpdlogLogger::SetFormatter(std::unique_ptr<spdlog::formatter> formatter)
{
    if (formatter == nullptr)
    {
        return;
    }

    for (auto& sink : m_sinks)
    {
        sink->SetFormatter(formatter->clone());
    }
}

void SpdlogLogger::SetPattern(const std::string& pattern)
{
    auto formatter = std::make_unique<spdlog::pattern_formatter>(pattern);
    SetFormatter(std::move(formatter));
}

void SpdlogLogger::SetAsDefaultLogger()
{
    spdlog::set_default_logger(m_logger);
}

const std::vector<SpdlogSink::Ptr>& SpdlogLogger::Sinks()
{
    return m_sinks;
}

void SpdlogLogger::Log(const std::chrono::system_clock::time_point& timepoint, Log::Level level, fmt::wstring_view msg)
{
    const auto utf8 = ToUtf8(std::wstring_view(msg.data(), msg.size()));
    Log(timepoint, level, utf8);
}

}  // namespace Log
}  // namespace Orc
