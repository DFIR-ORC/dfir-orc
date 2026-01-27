//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <fmt/core.h>
#include <fmt/xchar.h>

namespace Log {

enum class Level
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

void SetLogLevel(Level level);
Level GetLogLevel();

template <typename T>
inline T&& arg_to_fmt(T&& v)
{
    return std::forward<T>(v);
}

void log_impl(Level level, const std::string& message);
void log_wimpl(Level level, const std::wstring& message);

template <typename... Args>
inline void Print(Level level, std::string_view format_str, Args&&... args)
{
    try
    {
        auto message = fmt::format(fmt::runtime(format_str), arg_to_fmt(std::forward<Args>(args))...);
        log_impl(level, message);
    }
    catch (const fmt::format_error& e)
    {
        fmt::print(stderr, "[ERROR] Log format error: {}\n", e.what());
    }
}

template <typename... Args>
inline void Print(Level level, const std::wstring_view format_str, Args&&... args)
{
    try
    {
        auto message = fmt::format(fmt::runtime(format_str), arg_to_fmt(std::forward<Args>(args))...);
        log_wimpl(level, message);
    }
    catch (const fmt::format_error& e)
    {
        fmt::print(stderr, "[ERROR] Log format error: {}\n", e.what());
    }
}

template <typename... Args>
inline void Trace(Args&&... args)
{
    Print(Level::Trace, std::forward<Args>(args)...);
}

template <typename... Args>
inline void Debug(Args&&... args)
{
    Print(Level::Debug, std::forward<Args>(args)...);
}

template <typename... Args>
inline void Info(Args&&... args)
{
    Print(Level::Info, std::forward<Args>(args)...);
}

template <typename... Args>
inline void Warn(Args&&... args)
{
    Print(Level::Warning, std::forward<Args>(args)...);
}

template <typename... Args>
inline void Error(Args&&... args)
{
    Print(Level::Error, std::forward<Args>(args)...);
}

template <typename... Args>
inline void Critical(Args&&... args)
{
    Print(Level::Critical, std::forward<Args>(args)...);
}

}  // namespace Log
