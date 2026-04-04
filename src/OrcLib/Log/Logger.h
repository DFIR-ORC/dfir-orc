//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <set>

#include <spdlog/spdlog.h>
#include <fmt/xchar.h>

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
    // FIXME: flip to true once trace can be enabled on demand to limit cpu usage
    static constexpr bool kEnableDefaultTrace = false;

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
                    fmt::format_to(std::back_inserter(msg), std::forward<Arg0>(arg0), std::forward<Args>(args)...);
                }
                catch (const fmt::format_error&)
                {
                    assert(0 && "Failed to format log message");
                    logger->Log(timepoint, level, "Failed to format log message");
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
    inline void
    Log(FacilityIt first, FacilityIt last, Log::Level level, fmt::format_string<Args...>&& fmt, Args&&... args)
    {
        Log(first,
            last,
            std::chrono::system_clock::now(),
            level,
            std::forward<fmt::format_string<Args...>>(fmt),
            std::forward<Args>(args)...);
    }
    template <typename FacilityIt, typename... Args>
    inline void
    Log(FacilityIt first, FacilityIt last, Log::Level level, fmt::wformat_string<Args...>&& fmt, Args&&... args)
    {
        Log(first,
            last,
            std::chrono::system_clock::now(),
            level,
            std::forward<fmt::wformat_string<Args...>>(fmt),
            std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Trace(Facility id, fmt::format_string<Args...>&& fmt, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Trace)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Trace(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }
    }
    template <typename... Args>
    void Trace(Facility id, fmt::wformat_string<Args...>&& fmt, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Trace)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Trace(std::forward<fmt::wformat_string<Args...>>(fmt), std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Trace(fmt::format_string<Args...> fmt, Args&&... args)
    {
        if constexpr (kEnableDefaultTrace)
        {
            Log(std::cbegin(m_defaultFacilities),
                std::cend(m_defaultFacilities),
                Level::Trace,
                std::forward<fmt::format_string<Args...>>(fmt),
                std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Trace(fmt::wformat_string<Args...>&& fmt, Args&&... args)
    {
        if constexpr (kEnableDefaultTrace)
        {
            Log(std::cbegin(m_defaultFacilities),
                std::cend(m_defaultFacilities),
                Level::Trace,
                std::forward<fmt::wformat_string<Args...>>(fmt),
                std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Debug(Facility id, fmt::format_string<Args...>&& fmt, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Debug)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Debug(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }
    }
    template <typename... Args>
    void Debug(Facility id, fmt::wformat_string<Args...>&& fmt, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Debug)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Debug(std::forward<fmt::wformat_string<Args...>>(fmt), std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Debug(fmt::format_string<Args...>&& fmt, Args&&... args)
    {
        Log(std::cbegin(m_defaultFacilities),
            std::cend(m_defaultFacilities),
            Level::Debug,
            std::forward<fmt::format_string<Args...>>(fmt),
            std::forward<Args>(args)...);
    }
    template <typename... Args>
    void Debug(fmt::wformat_string<Args...>&& fmt, Args&&... args)
    {
        Log(std::cbegin(m_defaultFacilities),
            std::cend(m_defaultFacilities),
            Level::Debug,
            std::forward<fmt::wformat_string<Args...>>(fmt),
            std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Info(Facility id, fmt::format_string<Args...>&& fmt, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Info)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Info(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }
    }
    template <typename... Args>
    void Info(Facility id, fmt::wformat_string<Args...>&& fmt, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Info)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Info(std::forward<fmt::wformat_string<Args...>>(fmt), std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Info(fmt::format_string<Args...>&& fmt, Args&&... args)
    {
        Log(std::cbegin(m_defaultFacilities),
            std::cend(m_defaultFacilities),
            Level::Info,
            std::forward<fmt::format_string<Args...>>(fmt),
            std::forward<Args>(args)...);
    }
    template <typename... Args>
    void Info(fmt::wformat_string<Args...>&& fmt, Args&&... args)
    {
        Log(std::cbegin(m_defaultFacilities),
            std::cend(m_defaultFacilities),
            Level::Info,
            std::forward<fmt::wformat_string<Args...>>(fmt),
            std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Warn(Facility id, fmt::format_string<Args...>&& fmt, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Warning)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Warn(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }
    }
    template <typename... Args>
    void Warn(Facility id, fmt::wformat_string<Args...>&& fmt, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Warning)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Warn(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Warn(fmt::format_string<Args...>&& fmt, Args&&... args)
    {
        Log(std::cbegin(m_defaultFacilities),
            std::cend(m_defaultFacilities),
            Level::Warning,
            std::forward<fmt::format_string<Args...>>(fmt),
            std::forward<Args>(args)...);
    }
    template <typename... Args>
    void Warn(fmt::wformat_string<Args...>&& fmt, Args&&... args)
    {
        Log(std::cbegin(m_defaultFacilities),
            std::cend(m_defaultFacilities),
            Level::Warning,
            std::forward<fmt::wformat_string<Args...>>(fmt),
            std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Error(Facility id, fmt::format_string<Args...>&& fmt, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Error)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Error(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }
    }
    template <typename... Args>
    void Error(Facility id, fmt::wformat_string<Args...>&& fmt, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Error)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Error(std::forward<fmt::wformat_string<Args...>>(fmt), std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Error(fmt::format_string<Args...>&& fmt, Args&&... args)
    {
        Log(std::cbegin(m_defaultFacilities),
            std::cend(m_defaultFacilities),
            Level::Error,
            std::forward<fmt::format_string<Args...>>(fmt),
            std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Error(fmt::wformat_string<Args...>&& fmt, Args&&... args)
    {
        Log(std::cbegin(m_defaultFacilities),
            std::cend(m_defaultFacilities),
            Level::Error,
            std::forward<fmt::wformat_string<Args...>>(fmt),
            std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Critical(Facility id, fmt::format_string<Args...>&& fmt, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Critical)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Critical(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }
    }
    template <typename... Args>
    void Critical(Facility id, fmt::wformat_string<Args...>&& fmt, Args&&... args)
    {
        ++m_logCounters[static_cast<std::underlying_type_t<Level>>(Level::Critical)];

        auto& logger = Get(id);
        if (logger)
        {
            logger->Critical(std::forward<fmt::wformat_string<Args...>>(fmt), std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void Critical(fmt::format_string<Args...>&& fmt, Args&&... args)
    {
        Log(std::cbegin(m_defaultFacilities),
            std::cend(m_defaultFacilities),
            Level::Critical,
            std::forward<fmt::format_string<Args...>>(fmt),
            std::forward<Args>(args)...);
    }
    template <typename... Args>
    void Critical(fmt::wformat_string<Args...>&& fmt, Args&&... args)
    {
        Log(std::cbegin(m_defaultFacilities),
            std::cend(m_defaultFacilities),
            Level::Critical,
            std::forward<fmt::wformat_string<Args...>>(fmt),
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
