//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

// https://docs.microsoft.com/en-us/windows/win32/winsock/ipv6-enabled-client-code-2

#include "stdafx.h"

#include "Syslog.h"

#include <string>
#include <memory>
#include <system_error>

#include "Utils/Guard/Winsock.h"
#include "Utils/WinApi.h"
#include "Text/Iconv.h"
#include "Log/Log.h"
#include "UdpSocket.h"
#include "SystemDetails.h"

using namespace Orc;

namespace Orc {
namespace Log {

Syslog::Syslog(UdpSocket&& socket)
    : m_sockets()
{
    AddEndpoint(std::move(socket));
}

Syslog::Syslog(const std::string& host, const std::string& port, std::error_code& ec)
{
    AddEndpoint(host, port, ec);
}

void Syslog::AddEndpoint(UdpSocket&& socket)
{
    m_sockets.push_back(std::move(socket));
}

void Syslog::AddEndpoint(const std::string& host, const std::string& port, std::error_code& ec)
{
    UdpSocket socket(host, port, ec);
    if (ec)
    {
        Log::Debug("Failed to create socket to '{}:{}' [{}]", host, port, ec);
        return;
    }

    m_sockets.push_back(std::move(socket));
}

void Syslog::Send(const SyslogMessage& message, std::error_code& ec)
{
    Send(message.Build(), ec);
}

void Syslog::Send(std::string_view message, std::error_code& ec)
{
    for (auto& socket : m_sockets)
    {
        socket.Send(message, ec);
    }
}

}  // namespace Log
}  // namespace Orc
