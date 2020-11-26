//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <memory>

#include <spdlog/sinks/sink.h>

namespace Orc {
namespace Log {

class SpdlogSink
{
public:
    using Ptr = std::shared_ptr<SpdlogSink>;

    // Use a template to control over the sink creation (no custom formatter or level without using the provided API).
    template <typename T, typename... Args>
    static std::unique_ptr<SpdlogSink> Create(Args&&... args)
    {
        return std::make_unique<SpdlogSink>(std::make_unique<T>(std::forward<Args>(args)...));
    }

    void AddTo(spdlog::logger& logger) { logger.sinks().push_back(m_sink); }

    spdlog::level::level_enum Level() const { return m_sink->level(); }
    void SetLevel(spdlog::level::level_enum level) { m_sink->set_level(level); }

    std::unique_ptr<spdlog::formatter> CloneFormatter() const
    {
        if (m_formatter == nullptr)
        {
            return nullptr;
        }

        return m_formatter->clone();
    }

    void SetFormatter(std::unique_ptr<spdlog::formatter> formatter)
    {
        if (formatter == nullptr)
        {
            // spdlog sinks does not allow set_formatter with nullptr
            formatter = std::make_unique<spdlog::pattern_formatter>();
        }

        m_formatter = std::move(formatter);
        m_sink->set_formatter(m_formatter->clone());
    }

    void SetPattern(const std::string& pattern)
    {
        auto formatter = std::make_unique<spdlog::pattern_formatter>(pattern);
        SetFormatter(std::move(formatter));
    }

protected:
    SpdlogSink(std::unique_ptr<spdlog::sinks::sink> sink)
        : m_sink(std::move(sink))
    {
    }

protected:
    std::shared_ptr<spdlog::sinks::sink> m_sink;
    std::unique_ptr<spdlog::formatter> m_formatter;
};

}  // namespace Log
}  // namespace Orc
