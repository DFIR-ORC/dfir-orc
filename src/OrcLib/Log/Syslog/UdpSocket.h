//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <string>
#include <system_error>

namespace Orc {

namespace Guard {
class Winsock;
class Socket;
}  // namespace Guard

namespace Log {

class UdpSocket final
{
public:
    UdpSocket(const std::string& host, const std::string& port, std::error_code& ec);

    void Send(std::string_view buffer, std::error_code& ec);

private:
    void Connect(const std::string& host, const std::string& port, std::error_code& ec);

private:
    std::unique_ptr<Guard::Winsock> m_winsock;
    std::unique_ptr<Guard::Socket> m_socket;
};

}  // namespace Log
}  // namespace Orc
