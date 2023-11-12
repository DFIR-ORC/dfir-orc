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
#include "Text/Format.h"

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
        kSyslog,
        kUnitTest,
        kFacilityCount
    };

    Logger(std::initializer_list<std::pair<Facility, SpdlogLogger::Ptr>> loggers = {});

    uint64_t warningCount() const;
    uint64_t errorCount() const;
    uint64_t criticalCount() const;

    template <typename FacilityIt, typename Timepoint, typename Arg0, typename... Args>
    inline void
    Log(FacilityIt first, FacilityIt last, const Timepoint& timepoint, Level level, Arg0&& arg0, Args&&... args)
    {
        using CharT = Traits::underlying_char_type_t<Arg0>;

        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(level)];

        if (first == last)
        {
            return;
        }

        fmt::basic_memory_buffer<CharT> msg;
        for (auto it = first; it != last; ++it)
        {
            const SpdlogLogger::Ptr& logger = *it;
            if (msg.size() == 0)
            {
                try
                {
                    if constexpr (std::is_same_v<CharT, char>)
                    {
                        fmt::vformat_to(std::back_inserter(msg), arg0, fmt::make_format_args(args...));
                    }
                    else
                    {
                        fmt::vformat_to(std::back_inserter(msg), arg0, fmt::make_wformat_args(args...));
                    }
                }
                catch (const fmt::format_error&)
                {
                    assert(0 && "Failed to format log message");
                    logger->Log(timepoint, level, "Failed to format log message");
                    logger->Log(timepoint, level, arg0);
                    continue;
                }
            }

            logger->Log(timepoint, level, std::basic_string_view<CharT>(msg.data(), msg.size()));
        }

#ifdef _DEBUG
        msg.push_back('\n');
        msg.push_back('\0');

        if (level >= Level::Info)
        {
            if constexpr (std::is_same_v<CharT, char>)
            {
                ::OutputDebugStringA(msg.data());
            }
            else
            {
                ::OutputDebugStringW(msg.data());
            }
        }
#endif
    }

    template <typename FacilityIt, typename... Args>
    inline void Log(FacilityIt first, FacilityIt last, Log::Level level, Args&&... args)
    {
        Log(first, last, std::chrono::system_clock::now(), level, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Trace(Facility id, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Trace)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Trace(std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Trace(Args&&... args)
    {
        // FIXME: find a way to only enable trace when requested to limit cpu usage
        return;

        Log(std::cbegin(m_defaultFacilities),
            std::cend(m_defaultFacilities),
            Level::Trace,
            std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Debug(Facility id, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Debug)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Debug(std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Debug(Args&&... args)
    {
        Log(std::cbegin(m_defaultFacilities),
            std::cend(m_defaultFacilities),
            Level::Debug,
            std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Info(Facility id, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Info)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Info(std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Info(Args&&... args)
    {
        Log(std::cbegin(m_defaultFacilities), std::cend(m_defaultFacilities), Level::Info, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Warn(Facility id, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Warning)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Warn(std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Warn(Args&&... args)
    {
        Log(std::cbegin(m_defaultFacilities),
            std::cend(m_defaultFacilities),
            Level::Warning,
            std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Error(Facility id, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Error)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Error(std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Error(Args&&... args)
    {
        Log(std::cbegin(m_defaultFacilities),
            std::cend(m_defaultFacilities),
            Level::Error,
            std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Critical(Facility id, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Critical)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Critical(std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Critical(Args&&... args)
    {
        Log(std::cbegin(m_defaultFacilities),
            std::cend(m_defaultFacilities),
            Level::Critical,
            std::forward<Args>(args)...);
    }

    void Flush()
    {
        for (auto& logger : m_loggers)
        {
            if (logger)
            {
                logger->Flush();
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
    std::array<std::atomic<uint64_t>, static_cast<std::underlying_type_t<Level>>(Level::LevelCount)> m_logCounters;
};

using Facility = Logger::Facility;

}  // namespace Log

using Logger = Orc::Log::Logger;

}  // namespace Orc
