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
void Trace(Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Trace(std::forward<Args>(args)...);
    }
}

template <typename... Args>
void Debug(Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Debug(std::forward<Args>(args)...);
    }
}

template <typename... Args>
void Info(Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Info(std::forward<Args>(args)...);
    }
}

template <typename... Args>
void Warn(Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Warn(std::forward<Args>(args)...);
    }
}

template <typename... Args>
void Error(Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Error(std::forward<Args>(args)...);
    }
}

template <typename... Args>
void Critical(Args&&... args)
{
    auto& instance = DefaultLogger();
    if (instance)
    {
        instance->Critical(std::forward<Args>(args)...);
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
