//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl(ANSSI)
//

#include "stdafx.h"

#include "UtilitiesLogger.h"

#include <spdlog/cfg/env.h>

#ifdef ORC_BUILD_BOOST_STACKTRACE
#    include <boost/stacktrace.hpp>
#endif

#include "Utils/Result.h"

#include "Log/Syslog/SyslogSink.h"

using namespace Orc::Command;
using namespace Orc::Log;
using namespace Orc;

namespace {

std::shared_ptr<Command::UtilitiesLogger::ConsoleSink> CreateConsoleSink()
{
    auto sink = std::make_shared<Command::UtilitiesLogger::ConsoleSink>();
    sink->SetLevel(Log::Level::Critical);
    return sink;
}

std::shared_ptr<Command::UtilitiesLogger::FileSink> CreateFileSink()
{
    auto sink = std::make_shared<Command::UtilitiesLogger::FileSink>();

    // Allow all logs to be print, they will be filtered by the upstream level set by spdlog::set_level
    sink->SetLevel(Log::Level::Trace);
    return sink;
}

auto CreateSyslogSink()
{
    auto sink = std::make_shared<Command::UtilitiesLogger::SyslogSink>();

    // Allow all logs to be print, they will be filtered by the upstream level set by spdlog::set_level
    sink->SetLevel(Log::Level::Off);
    return sink;
}

std::tuple<SpdlogLogger::Ptr, SpdlogLogger::Ptr, SpdlogLogger::Ptr>
CreateFacilities(SpdlogSink::Ptr consoleSink, SpdlogSink::Ptr fileSink)
{
    auto default = UtilitiesLogger::CreateSpdlogLogger("default");
    default->Add(consoleSink);
    default->Add(fileSink);
    default->EnableBacktrace(64);
    default->SetFormatter(
        std::make_unique<spdlog::pattern_formatter>(Log::kDefaultLogPattern, spdlog::pattern_time_type::utc));

    auto file = UtilitiesLogger::CreateSpdlogLogger("file");
    file->Add(fileSink);
    file->SetFormatter(
        std::make_unique<spdlog::pattern_formatter>(Log::kDefaultLogPattern, spdlog::pattern_time_type::utc));

    auto journal = UtilitiesLogger::CreateSpdlogLogger("journal");
    journal->SetFormatter(
        std::make_unique<spdlog::pattern_formatter>(Log::kDefaultLogPattern, spdlog::pattern_time_type::utc));
    journal->SetLevel(Level::Off);
    journal->DisableBacktrace();

    return {std::move(default), std::move(file), std::move(journal)};
}

spdlog::sink_ptr CreateStderrSink()
{
    return std::make_unique<UtilitiesLogger::ConsoleSink::StderrSink>();
}

std::unique_ptr<spdlog::sinks::sink> CreateTeeSink()
{
    using TeeSink = UtilitiesLogger::ConsoleSink::TeeSink;
    using StderrSink = UtilitiesLogger::ConsoleSink::StderrSink;

    std::vector<spdlog::sink_ptr> sinks = {std::make_shared<StderrSink>()};
    return std::make_unique<TeeSink>(std::move(sinks));
}

}  // namespace

namespace Orc {
namespace Command {

std::unique_ptr<SpdlogLogger> UtilitiesLogger::CreateSpdlogLogger(const std::string& name)
{
    auto logger = std::make_unique<SpdlogLogger>(name);

    // This is error handler will help to fix log formatting error
    logger->SetErrorHandler([](const std::string& msg) {
        std::cerr << msg << std::endl;

#ifdef ORC_BUILD_BOOST_STACKTRACE
        std::cerr << "Stack trace:" << std::endl;
        std::cerr << boost::stacktrace::stacktrace();
#endif
    });

    // Default upstream log level filter (sinks will not received filtered logs)
    logger->SetLevel(Log::Level::Debug);

    return logger;
}

UtilitiesLogger::ConsoleSink::ConsoleSink()
    : SpdlogSink(::CreateTeeSink())
    , m_tee(reinterpret_cast<TeeSink&>(*m_sink))
{
}

UtilitiesLogger::UtilitiesLogger()
{
    m_fileSink = ::CreateFileSink();
    m_consoleSink = ::CreateConsoleSink();
    m_syslogSink = ::CreateSyslogSink();

    auto [default, file, journal] = ::CreateFacilities(m_consoleSink, m_fileSink);

    auto loggers = {
        std::make_pair(Log::Facility::kDefault, default),
        std::make_pair(Log::Facility::kLogFile, file),
        std::make_pair(Log::Facility::kJournal, journal)};

    m_logger = std::make_shared<Logger>(loggers);

    Orc::Log::SetDefaultLogger(m_logger);
}

UtilitiesLogger::~UtilitiesLogger()
{
    Orc::Log::SetDefaultLogger(nullptr);
}

}  // namespace Command
}  // namespace Orc
