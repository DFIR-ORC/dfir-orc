//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "log.h"

#include <fmt/color.h>
#include <fmt/chrono.h>

namespace Log {

std::string_view level_to_string(Level level) noexcept
{
    switch (level)
    {
        case Level::Trace:    return "TRACE";
        case Level::Debug:    return "DEBUG";
        case Level::Info:     return "INFO";
        case Level::Warning:  return "WARN";
        case Level::Error:    return "ERROR";
        case Level::Critical: return "CRIT";
    }
    return "UNKNOWN";
}

std::wstring_view level_to_wstring(Level level) noexcept
{
    switch (level)
    {
        case Level::Trace:    return L"TRACE";
        case Level::Debug:    return L"DEBUG";
        case Level::Info:     return L"INFO";
        case Level::Warning:  return L"WARN";
        case Level::Error:    return L"ERROR";
        case Level::Critical: return L"CRIT";
    }
    return L"UNKNOWN";
}

fmt::text_style get_level_style(Level level) noexcept
{
    switch (level)
    {
        case Level::Trace:    return fmt::fg(fmt::color::gray);
        case Level::Debug:    return fmt::fg(fmt::color::cyan);
        case Level::Info:     return fmt::fg(fmt::color::green);
        case Level::Warning:  return fmt::fg(fmt::color::yellow);
        case Level::Error:    return fmt::fg(fmt::color::red);
        case Level::Critical: return fmt::fg(fmt::color::red) | fmt::emphasis::bold;
    }
    return fmt::text_style();
}

// Portable thread-safe localtime conversion.
//
// Problem: fmt::localtime was introduced in a newer fmt release and is absent here.
// On MSVC with the v141_xp toolset, system_clock::to_time_t() returns __time64_t,
// which does not implicitly convert to the 32-bit time_t that plain localtime() expects.
// Using _localtime64_s() sidesteps the 32/64-bit ambiguity entirely.
//
// Performance note: for very high-frequency logging, cache the last-formatted second
// and only reformat when the time_t value changes (compare against a thread_local
// previously-seen timestamp). This avoids the struct tm fill + fmt format on every call.
static std::tm safe_localtime(std::chrono::system_clock::time_point tp)
{
    // Use __time64_t explicitly on MSVC to match what system_clock::to_time_t returns
    // and avoid the implicit-conversion error with the XP CRT's localtime().
#if defined(_MSC_VER)
    __time64_t t = static_cast<__time64_t>(std::chrono::system_clock::to_time_t(tp));
    std::tm result{};
    _localtime64_s(&result, &t);
    return result;
#else
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm result{};
    ::localtime_r(&t, &result);
    return result;
#endif
}

// Format current timestamp for log entries
std::string format_timestamp()
{
    std::tm tm = safe_localtime(std::chrono::system_clock::now());
    return fmt::format("{:%Y-%m-%d %H:%M:%S}", tm);
}

std::wstring format_wtimestamp()
{
    std::tm tm = safe_localtime(std::chrono::system_clock::now());
    return fmt::format(L"{:%Y-%m-%d %H:%M:%S}", tm);
}

static Level g_LogLevel = Level::Info;

void SetLogLevel(Level level)
{
    g_LogLevel = level;
}

Level GetLogLevel()
{
    return g_LogLevel;
}

void log_impl(Level level, const std::string& message)
{
    if (level < g_LogLevel)
        return;

    const std::string timestamp = format_timestamp();
    const std::string level_str{level_to_string(level)};

    fmt::print(stderr, "[{}] {} {}\n",
        timestamp,
        fmt::format(get_level_style(level), "[{:^5}]", level_str),
        message);
}

void log_wimpl(Level level, const std::wstring& message)
{
    if (level < g_LogLevel)
        return;

    const std::wstring timestamp = format_wtimestamp();
    const auto level_str{level_to_wstring(level)};

    fmt::print(stderr, L"[{}] {} {}\n",
        timestamp,
        fmt::format(get_level_style(level), L"[{:^5}]", level_str),
        message);
}

}  // namespace Log
