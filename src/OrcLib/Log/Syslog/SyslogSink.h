//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <spdlog/sinks/base_sink.h>

#include <string>
#include <memory>
#include <cassert>

#include <spdlog/details/null_mutex.h>
#include <spdlog/details/synchronous_factory.h>

#include "Log/Syslog/Syslog.h"
#include "SystemDetails.h"

namespace Orc {
namespace Log {

class UdpSocket;

template <typename Mutex>
class SyslogSink : public spdlog::sinks::base_sink<Mutex>
{
public:
    SyslogSink();

    SyslogSink(const SyslogSink&) = delete;
    SyslogSink& operator=(const SyslogSink&) = delete;

    void AddEndpoint(UdpSocket&& socket);
    void AddEndpoint(const std::string& host, const std::string& port, std::error_code& ec);

protected:
    // Prevent any formatter change as it would break syslog messages
    void set_formatter_(std::unique_ptr<spdlog::formatter> sink_formatter) override {}

    void sink_it_(const spdlog::details::log_msg& msg) override;
    void flush_() override {}

private:
    Syslog m_syslog;
    SyslogMessage m_message;
};

extern template class SyslogSink<std::mutex>;

}  // namespace Log
}  // namespace Orc
