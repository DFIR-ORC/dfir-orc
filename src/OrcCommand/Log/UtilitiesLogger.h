//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl(ANSSI)
//

#pragma once

#include "Log/Logger.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/ostream_sink.h>

#include "Log/FileSink.h"
#include "Log/Syslog/SyslogSink.h"

#pragma managed(push, off)

namespace Orc {

namespace Log {

class UdpSocket;

}

namespace Command {

struct UtilitiesLoggerConfiguration;

class UtilitiesLogger
{
public:
    class ConsoleSink : public Log::SpdlogSink
    {
    public:
        using TeeSink = spdlog::sinks::dist_sink_mt;
        using StderrSink = spdlog::sinks::wincolor_stderr_sink_st;

        ConsoleSink();

        void AddOutput(spdlog::sink_ptr sink)
        {
            sink->set_formatter(CloneFormatter());
            m_fileTee.add_sink(std::move(sink));
        }

        void AddOutput(std::shared_ptr<std::ostream> output)
        {
            using Sink = spdlog::sinks::ostream_sink_st;
            using Deleter = std::function<void(Sink*)>;

            std::unique_ptr<Sink, Deleter> sink(new Sink(*output), [output](Sink* s) { delete s; });
            AddOutput(std::move(sink));
        }

    private:
        TeeSink& m_fileTee;
    };

    class FileSink : public Log::SpdlogSink
    {
    public:
        using FileSinkT = Log::FileSink<std::mutex>;

        FileSink()
            : SpdlogSink(std::make_unique<FileSinkT>())
            , m_fileSink(reinterpret_cast<FileSinkT&>(*m_sink))
        {
        }

        void Open(const std::filesystem::path& path, FileDisposition disposition, std::error_code& ec)
        {
            m_fileSink.Open(path, disposition, ec);
        }

        bool IsOpen() const { return m_fileSink.IsOpen(); }
        void Close() { return m_fileSink.Close(); }
        std::optional<std::filesystem::path> OutputPath() const { return m_fileSink.OutputPath(); }

    private:
        FileSinkT& m_fileSink;
    };

    class SyslogSink : public Log::SpdlogSink
    {
    public:
        using SyslogSinkT = Log::SyslogSink<std::mutex>;

        SyslogSink()
            : SpdlogSink(std::make_unique<SyslogSinkT>())
            , m_syslogSink(reinterpret_cast<SyslogSinkT&>(*m_sink))
        {
        }

        void AddEndpoint(const std::string& host, const std::string& port, std::error_code& ec)
        {
            m_syslogSink.AddEndpoint(host, port, ec);
        }

        void AddEndpoint(Orc::Log::UdpSocket&& socket) { m_syslogSink.AddEndpoint(std::move(socket)); }

    private:
        SyslogSinkT& m_syslogSink;
    };

    UtilitiesLogger();
    ~UtilitiesLogger();

    Orc::Logger& logger() { return *m_logger; }
    const Orc::Logger& logger() const { return *m_logger; }

    const auto& fileSink() const { return m_fileSink; }
    const auto& consoleSink() const { return m_consoleSink; }
    const auto& syslogSink() const { return m_syslogSink; }

    static std::unique_ptr<Log::SpdlogLogger> CreateSpdlogLogger(const std::string& name);

private:
    std::shared_ptr<Orc::Logger> m_logger;
    std::shared_ptr<FileSink> m_fileSink;
    std::shared_ptr<ConsoleSink> m_consoleSink;
    std::shared_ptr<SyslogSink> m_syslogSink;
};

}  // namespace Command
}  // namespace Orc

#pragma managed(pop)
