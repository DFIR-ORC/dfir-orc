//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

namespace Orc {
namespace Log {

enum class SyslogFacility : uint8_t
{
    Kernel = 0,
    User,
    Mail,
    SystemDaemons,
    SecurityAuthorization_4,
    SyslogInternal,
    PrinterSubsystem,
    NetworkNewsSubsystem,
    UucpSubsystem,
    ClockDaemon_9,
    SecurityAuthorization_10,
    FtpDaemon,
    NtpSubsystem,
    LogAudit,
    LogAlert,
    ClockDaemon_15,
    LocalUse0,
    LocalUse1,
    LocalUse2,
    LocalUse3,
    LocalUse4,
    LocalUse5,
    LocalUse6,
    LocalUse7,
    Off
};

}  // namespace Log
}  // namespace Orc
