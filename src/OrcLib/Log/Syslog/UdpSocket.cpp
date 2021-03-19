//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "UdpSocket.h"

#include "Log/Log.h"

namespace Orc {
namespace Log {

UdpSocket::UdpSocket(const std::string& host, const std::string& port, std::error_code& ec)
    : m_socket(INVALID_SOCKET)
{
    m_winsock = Guard::Winsock::Create(2, 2, ec);
    if (ec)
    {
        Log::Error("Failed to initialize winsock environment: [{}]", ec);
        return;
    }

    Connect(host, port, ec);
    if (ec)
    {
        Log::Error("Failed to create UdpSocket: [{}]", ec);
        return;
    }
}

void UdpSocket::Send(std::string_view buffer, std::error_code& ec)
{
    auto ret = ::send(m_socket.value(), buffer.data(), buffer.size(), 0);
    if (ret == SOCKET_ERROR)
    {
        ec = LastWin32Error();
        Log::Error("Failed send() [{}]", ec);
        return;
    }
}

void UdpSocket::Connect(const std::string& host, const std::string& port, std::error_code& ec)
{
    Guard::AddrInfoA addrInfo;

    ADDRINFOA hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    auto ret = getaddrinfo(host.c_str(), port.c_str(), &hints, addrInfo.data());
    if (ret != ERROR_SUCCESS)
    {
        ec.assign(WSAGetLastError(), std::system_category());
        return;
    }

    ADDRINFO* AI;
    for (AI = addrInfo.get(); AI != NULL; AI = AI->ai_next)
    {
        Guard::Socket socket = ::socket(AI->ai_family, AI->ai_socktype, AI->ai_protocol);
        if (socket == INVALID_SOCKET)
        {
            Log::Error(L"Failed to create socket [{}]", LastWin32Error());
            continue;
        }

        ret = connect(*socket, AI->ai_addr, (int)AI->ai_addrlen);
        if (ret == SOCKET_ERROR)
        {
            Log::Error(L"Failed to connect socket [{}]", LastWin32Error());
            continue;
        }

        m_socket = std::move(socket);
        break;
    }

    if (AI == NULL)
    {
        Log::Error(L"Cannot find an interface to bind a socket on");
        ec = std::make_error_code(std::errc::protocol_error);
        return;
    }
}

}  // namespace Log
}  // namespace Orc
