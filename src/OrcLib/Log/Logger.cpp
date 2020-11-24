//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

//
// Logger is a wrapper over spdlog that dispatches logging on spdlog::logger.
// It add some features like warn, error, critical log count. Sink registration.
//

#include "stdafx.h"

#include "Log/Logger.h"

#include <fstream>

#include <spdlog/logger.h>

#ifdef ORC_BUILD_BOOST_STACKTRACE
#    include <boost/stacktrace.hpp>
#endif

using namespace Orc;

namespace {

std::shared_ptr<Logger::ConsoleSink> CreateConsoleSink()
{
    auto console = std::make_shared<Logger::ConsoleSink>();
    console->set_level(spdlog::level::critical);
    return console;
}

std::shared_ptr<Logger::FileSink> CreateFileSink()
{
    auto file = std::make_shared<Logger::FileSink>();

    // Allow all logs to be print, they will be filtered by the upstream level set by spdlog::set_level
    file->set_level(spdlog::level::trace);
    return file;
}

}  // namespace

namespace Orc {

Logger::Logger(std::initializer_list<std::pair<Facility, std::shared_ptr<spdlog::logger>>> loggers)
    : m_warningCount(0)
    , m_errorCount(0)
    , m_criticalCount(0)
{
    auto facility_count = std::underlying_type_t<Facility>(Facility::kFacilityCount);
    m_loggers.resize(facility_count - 1);

    for (auto&& [facility, logger] : loggers)
    {
        auto idx = std::underlying_type_t<Facility>(facility);
        assert(idx < facility_count);
        if (!m_loggers[idx] && logger)
        {
            m_loggers[idx] = std::move(logger);
        }
    }

    spdlog::set_default_logger(Logger::Get(Facility::kDefault));

    // Default upstream log level filter (sinks will not received filtered logs)
    spdlog::set_level(spdlog::level::debug);

    spdlog::enable_backtrace(512);

    // This is error handler will help to fix log formatting error
    spdlog::set_error_handler([](const std::string& msg) {
        std::cerr << msg << std::endl;

#ifdef ORC_BUILD_BOOST_STACKTRACE
        std::cerr << "Stack trace:" << std::endl;
        std::cerr << boost::stacktrace::stacktrace();
#endif
    });

    // https://github.com/gabime/spdlog/wiki/3.-Custom-formatting
    // The following could output: '2020-09-30T13:43:41.256Z [I] this is a foobar log'
    // The %^...%$ options specify coloring range, only one is currently supported
    spdlog::set_pattern("%^%Y-%m-%dT%T.%eZ [%L] %v%$", spdlog::pattern_time_type::utc);
}

uint64_t Logger::warningCount() const
{
    return m_warningCount;
}

uint64_t Logger::errorCount() const
{
    return m_errorCount;
}

uint64_t Logger::criticalCount() const
{
    return m_criticalCount;
}

// TODO: This feature is only supported by default logger. To be able to use a backtrace with multiple loggers a
// dedicated sink should be done so all logs would be automatically sorted.
void Logger::DumpBacktrace()
{
    const auto& sinks = Get(Facility::kDefault)->sinks();

    std::vector<spdlog::level::level_enum> levels;
    for (size_t i = 0; i < sinks.size(); ++i)
    {
        levels.push_back(sinks[i]->level());

        // set trace level to ensure the stack of logs will be displayed even if configurated level would be too high
        sinks[i]->set_level(spdlog::level::trace);
    }

    spdlog::dump_backtrace();

    for (size_t i = 0; i < sinks.size(); ++i)
    {
        sinks[i]->set_level(levels[i]);
    }
}

void Logger::Set(Facility id, std::shared_ptr<spdlog::logger> logger)
{
    const auto facilityNum = std::underlying_type_t<Facility>(id);
    if (m_loggers.size() <= facilityNum)
    {
        m_loggers.resize(facilityNum + 1);
    }

    m_loggers[facilityNum] = logger;
}

}  // namespace Orc
