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
#include "Text/Iconv.h"

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
    void Print(const std::wstring_view& commandSet, const std::wstring_view& agent, FmtArgs&&... status) const
    {
        const auto timepoint = std::chrono::system_clock::now();

        std::wstring message;
        Text::FormatToWithoutEOL(std::back_inserter(message), "{:<16} {:<26} ", commandSet, agent);
        Text::FormatToWithoutEOL(std::back_inserter(message), std::forward<FmtArgs>(status)...);

        // TODO: instead of using console directly the journal facility could have a custom console sink
        std::time_t now = std::chrono::system_clock::to_time_t(timepoint);
        {
            std::lock_guard lock(m_mutex);
            m_console.Print(L"{:%Y-%m-%dT%H:%M:%SZ}   {}", fmt::gmtime(now), message);
        }

        const auto& journal = Orc::Log::DefaultLogger()->Get(Log::Facility::kJournal);
        if (journal)
        {
            journal->Log(timepoint, Log::Level::Info, Utf16ToUtf8(message, "<unicode conversion failed>"));
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
