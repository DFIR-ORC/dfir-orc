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

#include "Console.h"

#include "UploadNotification.h"

namespace Orc::Command::Wolf {

class Journal
{
public:
    Journal::Journal(Console& console)
        : m_mutex()
        , m_console(console)
    {
    }

    template <typename... FmtArgs>
    void Print(const std::wstring& commandSet, const std::wstring& agent, FmtArgs&&... status) const
    {
        std::wstring message;
        Text::FormatToWithoutEOL(std::back_inserter(message), "{:<16} {:<26} ", commandSet, agent);
        Text::FormatToWithoutEOL(std::back_inserter(message), std::forward<FmtArgs>(status)...);

        // TODO: instead of using console directly the journal facility could have a custom console sink
        {
            std::lock_guard lock(m_mutex);
            std::time_t now = std::time(nullptr);
            m_console.Print(L"{:%Y-%m-%dT%H:%M:%SZ}   {}", fmt::gmtime(now), message);
        }

        const auto& journal = Orc::Log::DefaultLogger()->Get(Log::Facility::kJournal);
        if (journal)
        {
            journal->Info(L"{}", message);
        }
    }

    auto Console() { return std::pair<std::lock_guard<std::mutex>, Command::Console&> {m_mutex, m_console}; }
    auto Console() const
    {
        return std::pair<std::lock_guard<std::mutex>, const Command::Console&> {m_mutex, m_console};
    }

private:
    mutable std::mutex m_mutex;
    Command::Console& m_console;
};

}  // namespace Orc::Command::Wolf
