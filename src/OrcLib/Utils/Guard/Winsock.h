//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Utils/Guard.h"

#include <winsock2.h>
#include <ws2tcpip.h>

namespace Orc {
namespace Guard {

class AddrInfoA final : public PointerGuard<ADDRINFOA>
{
public:
    ~AddrInfoA()
    {
        if (m_data)
        {
            freeaddrinfo(m_data);
        }
    }
};

class Socket final : public DescriptorGuard<SOCKET>
{
public:
    Socket(SOCKET socket = INVALID_SOCKET)
        : DescriptorGuard<SOCKET>(socket, INVALID_SOCKET)
    {
    }

    Socket(Socket&& o) = default;
    Socket& operator=(Socket&& o) = default;

    ~Socket()
    {
        if (m_data != m_invalidValue)
        {
            closesocket(m_data);
        }
    }
};

class Winsock final
{
public:
    using Ptr = std::shared_ptr<Winsock>;

    static std::unique_ptr<Winsock> Create(uint8_t versionMajor, uint8_t versionMinor, std::error_code& ec);

    ~Winsock();

private:
    Winsock(const WSADATA& wsaData);

private:
    WSADATA m_wsaData;
};

}  // namespace Guard
}  // namespace Orc
