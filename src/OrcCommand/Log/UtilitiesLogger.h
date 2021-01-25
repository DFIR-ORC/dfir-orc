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

#include <spdlog/sinks/stdout_color_sinks.h>
#include "Log/Sink/FileSink.h"

#pragma managed(push, off)

namespace Orc {
namespace Command {

class UtilitiesLogger
{
public:
    class ConsoleSink : public Log::SpdlogSink
    {
    public:
        ConsoleSink()
            : SpdlogSink(std::make_unique<spdlog::sinks::wincolor_stderr_sink_mt>())
        {
        }
    };

    class FileSink : public Log::SpdlogSink
    {
    public:
        using FileSinkT = Log::FileSink<std::mutex>;

        FileSink()
            : SpdlogSink(std::make_unique<FileSinkT>())
            , m_fileSink(reinterpret_cast<FileSinkT*>(m_sink.get()))
        {
        }

        void Open(const std::filesystem::path& path, FileDisposition disposition, std::error_code& ec)
        {
            m_fileSink->Open(path, disposition, ec);
        }

        bool IsOpen() const { return m_fileSink->IsOpen(); }
        void Close() { return m_fileSink->Close(); }
        std::optional<std::filesystem::path> OutputPath() const { return m_fileSink->OutputPath(); }

    private:
        FileSinkT* m_fileSink;
    };

    UtilitiesLogger();
    ~UtilitiesLogger();

    void Configure(int argc, const wchar_t* argv[]) const;

    Orc::Logger& logger() { return *m_logger; }
    const Orc::Logger& logger() const { return *m_logger; }

    const auto& fileSink() const { return m_fileSink; }
    const auto& consoleSink() const { return m_consoleSink; }

private:
    std::shared_ptr<Orc::Logger> m_logger;
    std::shared_ptr<FileSink> m_fileSink;
    std::shared_ptr<ConsoleSink> m_consoleSink;
};

}  // namespace Command
}  // namespace Orc

#pragma managed(pop)
