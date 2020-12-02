//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
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
const std::string kDefaultLogPattern("%^%Y-%m-%dT%T.%eZ [%L] %v%$");

class SpdlogLogger
{
public:
    using Ptr = std::shared_ptr<SpdlogLogger>;

    SpdlogLogger(const std::string& name);

    void Add(SpdlogSink::Ptr sink);

    spdlog::level::level_enum Level() const;
    void SetLevel(spdlog::level::level_enum level);

    void SetPattern(const std::string& pattern);
    void SetFormatter(std::unique_ptr<spdlog::formatter> formatter);

    void EnableBacktrace(size_t messageCount);
    void DisableBacktrace();
    void DumpBacktrace();

    void SetErrorHandler(std::function<void(const std::string&)> handler);

    void SetAsDefaultLogger();

    const std::vector<SpdlogSink::Ptr>& Sinks();

    template <typename... Args>
    void Trace(Args&&... args)
    {
        m_logger->trace(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Debug(Args&&... args)
    {
        m_logger->debug(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Info(Args&&... args)
    {
        m_logger->info(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Warn(Args&&... args)
    {
        m_logger->warn(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Error(Args&&... args)
    {
        m_logger->error(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Critical(Args&&... args)
    {
        m_logger->critical(std::forward<Args>(args)...);
    }

private:
    std::shared_ptr<spdlog::logger> m_logger;
    std::unique_ptr<spdlog::formatter> m_backtraceFormatter;
    std::vector<SpdlogSink::Ptr> m_sinks;
};

}  // namespace Log
}  // namespace Orc
