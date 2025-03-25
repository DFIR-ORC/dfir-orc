//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
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

#include "Log/FileSink.h"
#include "Log/MemorySink.h"

using namespace Orc::Log;
using namespace Orc;

Logger::Logger(std::initializer_list<std::pair<Facility, SpdlogLogger::Ptr>> loggers)
    : m_loggers()
    , m_defaultFacilities()
    , m_logCounters()
{
    std::fill(std::begin(m_logCounters), std::end(m_logCounters), 0);

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

    auto& defaultLogger = m_loggers[0];
    if (defaultLogger)
    {
        defaultLogger->SetAsDefaultLogger();
    }
}

uint64_t Logger::warningCount() const
{
    return m_logCounters[std::underlying_type_t<Level>(Level::Warning)];
}

uint64_t Logger::errorCount() const
{
    return m_logCounters[std::underlying_type_t<Level>(Level::Error)];
}

uint64_t Logger::criticalCount() const
{
    return m_logCounters[std::underlying_type_t<Level>(Level::Critical)];
}

void Logger::Set(Facility id, SpdlogLogger::Ptr logger)
{
    const auto facilityNum = std::underlying_type_t<Facility>(id);
    if (m_loggers.size() <= facilityNum)
    {
        m_loggers.resize(facilityNum + 1);
    }

    m_loggers[facilityNum] = std::move(logger);
}
