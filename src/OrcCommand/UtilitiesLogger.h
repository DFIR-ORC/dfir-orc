//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl(ANSSI)
//

#pragma once

#include "Log/Logger.h"

#pragma managed(push, off)

namespace Orc {

namespace Command {

class UtilitiesLogger
{
public:
    UtilitiesLogger();
    ~UtilitiesLogger();

    void Configure(int argc, const wchar_t* argv[]) const;

    auto OpenFile(const std::filesystem::path& path, std::error_code& ec) const
    {
        assert(m_fileSink);
        m_fileSink->Open(path, ec);
    }

    Orc::Logger& logger() { return *m_logger; }
    const Orc::Logger& logger() const { return *m_logger; }

    const auto& fileSink() const { return m_fileSink; }
    const auto& consoleSink() const { return m_consoleSink; }

private:
    std::shared_ptr<Orc::Logger> m_logger;
    std::shared_ptr<Orc::Logger::FileSink> m_fileSink;
    std::shared_ptr<Orc::Logger::ConsoleSink> m_consoleSink;
};

}  // namespace Command
}  // namespace Orc

#pragma managed(pop)
