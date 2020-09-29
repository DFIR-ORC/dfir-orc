//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "stdafx.h"

#include "Log/Logger.h"

#include <fstream>

#include <spdlog/logger.h>
#include <boost/stacktrace.hpp>

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

Logger::Logger()
    : m_consoleSink(::CreateConsoleSink())
    , m_fileSink(::CreateFileSink())
    , m_logger(new spdlog::logger("default", {m_consoleSink, m_fileSink}))
    , m_warningCount(0)
    , m_errorCount(0)
    , m_criticalCount(0)
{
    spdlog::set_default_logger(m_logger);

    // Default upstream log level filter (sinks will not received filtered logs)
    spdlog::set_level(spdlog::level::debug);

    spdlog::enable_backtrace(512);

    // This is error handler will help to fix log formatting error
    spdlog::set_error_handler([](const std::string& msg) {
        std::cerr << msg << std::endl;
        std::cerr << "Stack trace:" << std::endl;
        std::cerr << boost::stacktrace::stacktrace();
    });

    // https://github.com/gabime/spdlog/wiki/3.-Custom-formatting
    // The following could output: '[17:49:47.335][I] this is a foobar log'
    // The %^...%$ options specify coloring range, only one is currently supported
    spdlog::set_pattern("%^[%H:%M:%S.%e][%L] %v%$");
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

void Logger::DumpBacktrace()
{
    const auto& sinks = m_logger->sinks();

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

void Logger::OpenOutputFile(const std::filesystem::path& path, std::error_code& ec)
{
    m_fileSink->Open(path, ec);
}

const Logger::FileSink& Logger::fileSink() const
{
    assert(m_fileSink);
    return *m_fileSink;
}

Logger::FileSink& Logger::fileSink()
{
    assert(m_fileSink);
    return *m_fileSink;
}

const Logger::ConsoleSink& Logger::consoleSink() const
{
    assert(m_consoleSink);
    return *m_consoleSink;
}

Logger::ConsoleSink& Logger::consoleSink()
{
    assert(m_consoleSink);
    return *m_consoleSink;
}

}  // namespace Orc
