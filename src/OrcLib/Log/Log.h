//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
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

template <typename... Args>
void Trace(const Args&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Trace(args...);
    }
}

template <typename... Args>
void Debug(const Args&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Debug(args...);
    }
}

template <typename... Args>
void Info(const Args&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Info(args...);
    }
}

template <typename... Args>
void Warn(const Args&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Warn(args...);
    }
}

template <typename... Args>
void Error(const Args&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Error(args...);
    }
}

template <typename... Args>
void Critical(const Args&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Critical(args...);
    }
}

}  // namespace Log
}  // namespace Orc
