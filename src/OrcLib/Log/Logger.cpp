//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright � 2020 ANSSI. All Rights Reserved.
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
{
    spdlog::set_default_logger(m_logger);

    // Default upstream log level filter (sinks will not received filtered logs)
    spdlog::set_level(spdlog::level::debug);

    //spdlog::enable_backtrace(64);

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