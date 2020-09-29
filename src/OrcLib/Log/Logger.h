//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "Log/MemorySink.h"
#include "Log/FileSink.h"

namespace Orc {

/*!
 * \brief Wrapper over spdlog that dispatches logging on spdlog::logger.
 *
 * It add some features like warn, error, critical log count. Sink registration.
 */
class Logger
{
public:
    using ConsoleSink = spdlog::sinks::wincolor_stderr_sink_mt;
    using FileSink = FileSink<std::mutex>;

    Logger();

    void OpenOutputFile(const std::filesystem::path& path, std::error_code& ec);

    const FileSink& fileSink() const;
    FileSink& fileSink();

    const ConsoleSink& consoleSink() const;
    ConsoleSink& consoleSink();

    void DumpBacktrace();

    template <typename... Args>
    void Trace(const Args&... args)
    {
        m_logger->trace(args...);
    }

    template <typename... Args>
    void Debug(const Args&... args)
    {
        m_logger->debug(args...);
    }

    template <typename... Args>
    void Info(const Args&... args)
    {
        m_logger->info(args...);
    }

    template <typename... Args>
    void Warn(const Args&... args)
    {
        m_logger->warn(args...);
    }

    template <typename... Args>
    void Error(const Args&... args)
    {
        m_logger->error(args...);
    }

    template <typename... Args>
    void Critical(const Args&... args)
    {
        m_logger->critical(args...);
    }

private:
    std::shared_ptr<ConsoleSink> m_consoleSink;
    std::shared_ptr<FileSink> m_fileSink;
    std::shared_ptr<spdlog::logger> m_logger;
};

}  // namespace Orc
