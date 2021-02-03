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

using namespace Orc::Command;
using namespace Orc::Log;
using namespace Orc;

namespace {

std::unique_ptr<SpdlogLogger> CreateSpdlogLogger(const std::string& name)
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

std::pair<SpdlogLogger::Ptr, SpdlogLogger::Ptr> CreateFacilities(SpdlogSink::Ptr consoleSink, SpdlogSink::Ptr fileSink)
{
    std::vector<std::shared_ptr<spdlog::logger>> loggers;

    auto defaultLogger = ::CreateSpdlogLogger("default");
    defaultLogger->Add(consoleSink);
    defaultLogger->Add(fileSink);
    defaultLogger->EnableBacktrace(512);
    defaultLogger->SetFormatter(
        std::make_unique<spdlog::pattern_formatter>(Log::kDefaultLogPattern, spdlog::pattern_time_type::utc));

    auto fileLogger = ::CreateSpdlogLogger("file");
    fileLogger->Add(fileSink);
    fileLogger->SetFormatter(
        std::make_unique<spdlog::pattern_formatter>(Log::kDefaultLogPattern, spdlog::pattern_time_type::utc));

    return {std::move(defaultLogger), std::move(fileLogger)};
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

UtilitiesLogger::ConsoleSink::ConsoleSink()
    : SpdlogSink(::CreateTeeSink())
    , m_tee(reinterpret_cast<TeeSink&>(*m_sink))
{
}

UtilitiesLogger::UtilitiesLogger()
{
    m_fileSink = ::CreateFileSink();
    m_consoleSink = ::CreateConsoleSink();

    auto [defaultLogger, fileLogger] = ::CreateFacilities(m_consoleSink, m_fileSink);

    auto loggers = {
        std::make_pair(Log::Facility::kDefault, defaultLogger), std::make_pair(Log::Facility::kLogFile, fileLogger)};
    m_logger = std::make_shared<Logger>(loggers);

    Orc::Log::SetDefaultLogger(m_logger);
}

UtilitiesLogger::~UtilitiesLogger()
{
    Orc::Log::SetDefaultLogger(nullptr);
}

}  // namespace Command
}  // namespace Orc
