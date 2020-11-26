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

#include "Log/Sink/MemorySink.h"
#include "Log/Sink/FileSink.h"

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

    enum class Facility : size_t
    {
        kDefault = 0,
        kLogFile,
        kUnitTest,
        kFacilityCount
    };

    Logger(std::initializer_list<std::pair<Facility, std::shared_ptr<spdlog::logger>>> loggers);

    uint64_t warningCount() const;
    uint64_t errorCount() const;
    uint64_t criticalCount() const;

    void DumpBacktrace();

    template <typename... Args>
    void Trace(Facility id, Args&&... args)
    {
        Get(id)->trace(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Trace(Args&&... args)
    {
        Trace(Facility::kDefault, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Debug(Facility id, Args&&... args)
    {
        Get(id)->debug(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Debug(Args&&... args)
    {
        Debug(Facility::kDefault, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Info(Facility id, Args&&... args)
    {
        Get(id)->info(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Info(Args&&... args)
    {
        Info(Facility::kDefault, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Warn(Facility id, Args&&... args)
    {
        Get(id)->warn(std::forward<Args>(args)...);
        ++m_warningCount;
    }

    template <typename... Args>
    void Warn(Args&&... args)
    {
        Warn(Facility::kDefault, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Error(Facility id, Args&&... args)
    {
        Get(id)->error(std::forward<Args>(args)...);
        ++m_errorCount;
    }

    template <typename... Args>
    void Error(Args&&... args)
    {
        Error(Facility::kDefault, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Critical(Facility id, Args&&... args)
    {
        Get(id)->critical(std::forward<Args>(args)...);
        ++m_criticalCount;

        DumpBacktrace();
    }

    template <typename... Args>
    void Critical(Args&&... args)
    {
        Critical(Facility::kDefault, std::forward<Args>(args)...);
    }

    const std::shared_ptr<spdlog::logger>& Get(Facility id) const
    {
        const auto facilityNumber = std::underlying_type_t<Facility>(id);

        assert(facilityNumber < m_loggers.size());
        auto& logger = m_loggers[facilityNumber];

        assert(logger);
        return logger;
    }

    void Set(Facility id, std::shared_ptr<spdlog::logger> logger);

private:
    std::vector<std::shared_ptr<spdlog::logger>> m_loggers;

    std::atomic<uint64_t> m_warningCount;
    std::atomic<uint64_t> m_errorCount;
    std::atomic<uint64_t> m_criticalCount;
};

}  // namespace Orc
