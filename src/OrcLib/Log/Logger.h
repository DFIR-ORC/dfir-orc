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
#include <set>

#include <spdlog/spdlog.h>

#include "SpdlogLogger.h"

namespace Orc {
namespace Log {

/*!
 * \brief Wrapper over spdlog that dispatches logging on spdlog::logger.
 *
 * It add some features like warn, error, critical log count. Sink registration.
 */

class Logger
{
public:
    enum class Facility : size_t
    {
        kConsole = 0,
        kLogFile,
        kJournal,
        kUnitTest,
        kFacilityCount
    };

    Logger(std::initializer_list<std::pair<Facility, SpdlogLogger::Ptr>> loggers);

    uint64_t warningCount() const;
    uint64_t errorCount() const;
    uint64_t criticalCount() const;

    template <typename... Args>
    void Trace(Facility id, Args&&... args)
    {
        auto& logger = Get(id);
        if (logger)
        {
            logger->Trace(std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Trace(Args&&... args)
    {
        for (auto& logger : m_defaultFacilities)
        {
            logger->Trace(std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Debug(Facility id, Args&&... args)
    {
        auto& logger = Get(id);
        if (logger)
        {
            logger->Debug(std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Debug(Args&&... args)
    {
        for (auto& logger : m_defaultFacilities)
        {
            logger->Debug(std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Info(Facility id, Args&&... args)
    {
        auto& logger = Get(id);
        if (logger)
        {
            logger->Info(std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Info(Args&&... args)
    {
        for (auto& logger : m_defaultFacilities)
        {
            logger->Info(std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Warn(Facility id, Args&&... args)
    {
        auto& logger = Get(id);
        if (logger)
        {
            logger->Warn(std::forward<Args>(args)...);
        }

        ++m_warningCount;
    }

    template <typename... Args>
    void Warn(Args&&... args)
    {
        for (auto& logger : m_defaultFacilities)
        {
            logger->Warn(std::forward<Args>(args)...);
        }

        ++m_warningCount;
    }

    template <typename... Args>
    void Error(Facility id, Args&&... args)
    {
        auto& logger = Get(id);
        if (logger)
        {
            logger->Error(std::forward<Args>(args)...);
            logger->DumpBacktrace(SpdlogLogger::BacktraceDumpReason::Error);
        }

        ++m_errorCount;
    }

    template <typename... Args>
    void Error(Args&&... args)
    {
        for (auto& logger : m_defaultFacilities)
        {
            logger->Error(std::forward<Args>(args)...);
            logger->DumpBacktrace(SpdlogLogger::BacktraceDumpReason::Error);
        }

        ++m_errorCount;
    }

    template <typename... Args>
    void Critical(Facility id, Args&&... args)
    {
        ++m_criticalCount;

        auto& logger = Get(id);
        if (logger)
        {
            logger->Critical(std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Critical(Args&&... args)
    {
        for (auto& logger : m_defaultFacilities)
        {
            logger->Critical(std::forward<Args>(args)...);
            logger->DumpBacktrace(SpdlogLogger::BacktraceDumpReason::CriticalError);
        }

        ++m_criticalCount;
    }

    void Flush()
    {
        for (auto& logger : m_loggers)
        {
            if (logger)
            {
                logger->Flush();
            }
            else
            {
                assert(false && "Unexpected null logger");
            }
        }
    }

    const SpdlogLogger::Ptr& Get(Facility id) const
    {
        const auto facilityNumber = std::underlying_type_t<Facility>(id);

        assert(facilityNumber < m_loggers.size());
        auto& logger = m_loggers[facilityNumber];

        assert(logger);
        return logger;
    }

    void Set(Facility id, SpdlogLogger::Ptr logger);

    void AddToDefaultFacilities(Facility id)
    {
        auto& logger = Get(id);
        assert(logger);
        if (logger)
        {
            m_defaultFacilities.insert(logger);
        }
    }

private:
    std::vector<SpdlogLogger::Ptr> m_loggers;
    std::set<SpdlogLogger::Ptr> m_defaultFacilities;

    std::atomic<uint64_t> m_warningCount;
    std::atomic<uint64_t> m_errorCount;
    std::atomic<uint64_t> m_criticalCount;
};

using Facility = Logger::Facility;

}  // namespace Log

using Logger = Orc::Log::Logger;

}  // namespace Orc
