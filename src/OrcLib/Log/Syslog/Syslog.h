//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <memory>
#include <optional>

#include <spdlog/sinks/sink.h>

#include "Log/Syslog/SyslogMessage.h"
#include "Log/Syslog/UdpSocket.h"

namespace Orc {
namespace Log {

class Syslog
{
public:
    Syslog() = default;

    explicit Syslog(UdpSocket&& socket);

    Syslog(const std::string& host, const std::string& port, std::error_code& ec);

    void AddEndpoint(UdpSocket&& socket);
    void AddEndpoint(const std::string& host, const std::string& port, std::error_code& ec);

    void Send(const SyslogMessage& message, std::error_code& ec);
    void Send(std::string_view message, std::error_code& ec);

private:
    std::vector<UdpSocket> m_sockets;
};

}  // namespace Log
}  // namespace Orc
