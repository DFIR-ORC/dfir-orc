//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <memory>

#include "Logger.h"

namespace Orc {
namespace Log {

std::shared_ptr<Logger>& DefaultLogger();

void SetDefaultLogger(std::shared_ptr<Logger> instance);

const SpdlogLogger::Ptr& Get(Facility id);

template <typename... Args>
void Trace(fmt::format_string<Args...>&& fmt, Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Trace(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}
template <typename... Args>
void Trace(fmt::wformat_string<Args...>&& fmt, Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Trace(std::forward<fmt::wformat_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}

template <typename... Args>
void Debug(fmt::format_string<Args...>&& fmt, Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Debug(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}
template <typename... Args>
void Debug(fmt::wformat_string<Args...>&& fmt, Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Debug(std::forward<fmt::wformat_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}

template <typename... Args>
void Info(fmt::format_string<Args...> && fmt, Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Info(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}

template <typename... Args>
void Info(Log::Facility facility, fmt::format_string<Args...>&& fmt, Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Info(facility, std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}

template <typename... Args>
void Info(fmt::wformat_string<Args...>&& fmt, Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Info(std::forward<fmt::wformat_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}


template <typename... Args>
void Info(Log::Facility facility, fmt::wformat_string<Args...>&& fmt, Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Info(facility, std::forward<fmt::wformat_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}

template <typename... Args>
void Warn(fmt::format_string<Args...>&& fmt, Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Warn(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}
template <typename... Args>
void Warn(fmt::wformat_string<Args...>&& fmt, Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Warn(std::forward<fmt::wformat_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}

template <typename... Args>
void Error(fmt::format_string<Args...>&& fmt, Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Error(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}

template <typename... Args>
void Error(fmt::wformat_string<Args...>&& fmt, Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Error(std::forward<fmt::wformat_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}

template <typename... Args>
void Critical(fmt::format_string<Args...>&& fmt, Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Critical(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}

template <typename... Args>
void Critical(fmt::wformat_string<Args...>&& fmt, Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Critical(std::forward<fmt::wformat_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}

inline void Flush()
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Flush();
    }
}

}  // namespace Log
}  // namespace Orc
