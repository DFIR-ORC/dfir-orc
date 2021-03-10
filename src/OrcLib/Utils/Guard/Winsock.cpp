//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Utils/Guard/Winsock.h"

namespace Orc {
namespace Guard {

Winsock::Winsock(const WSADATA& wsaData)
    : m_wsaData(wsaData)
{
}

Winsock ::~Winsock()
{
    WSACleanup();
}

std::unique_ptr<Winsock> Winsock::Create(uint8_t versionMajor, uint8_t versionMinor, std::error_code& ec)
{
    WSADATA wsaData;
    auto ret = WSAStartup(MAKEWORD(versionMajor, versionMinor), &wsaData);
    if (ret != ERROR_SUCCESS)
    {
        ec.assign(WSAGetLastError(), std::system_category());
        return {};
    }

    return std::unique_ptr<Winsock>(new Winsock(wsaData));
}

}  // namespace Guard
}  // namespace Orc
