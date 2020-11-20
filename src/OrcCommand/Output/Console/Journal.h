//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include <fmt/chrono.h>

#include "Output/Console/Console.h"

#include "UploadNotification.h"

namespace OrcCommand {
namespace Output {

class Journal
{
public:
    Journal::Journal(Orc::Output::Console& console)
        : m_mutex()
        , m_console(console)
    {
    }

    template <typename... FmtArgs>
    void Print(const std::wstring& commandSet, const std::wstring& agent, FmtArgs&&... status) const
    {
        std::time_t now = std::time(nullptr);

        {
            std::lock_guard lock(m_mutex);
            m_console.Write(L"{:%Y-%m-%dT%H:%M.%SZ}   {:<16} {:<26} ", fmt::gmtime(now), commandSet, agent);
            m_console.Print(std::forward<FmtArgs>(status)...);
        }
    }

    auto Console() { return std::pair<std::lock_guard<std::mutex>, Orc::Output::Console&> {m_mutex, m_console}; }
    auto Console() const
    {
        return std::pair<std::lock_guard<std::mutex>, const Orc::Output::Console&> {m_mutex, m_console};
    }

private:
    mutable std::mutex m_mutex;
    Orc::Output::Console& m_console;
};

}  // namespace Output
}  // namespace OrcCommand
