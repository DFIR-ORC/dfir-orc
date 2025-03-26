//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "stdafx.h"

#include "SyslogMessage.h"

#include <fmt/core.h>

#include "Utils/Time.h"

namespace {

void SetOptionalString(const std::string& input, std::optional<std::string>& output)
{
    if (input.empty())
    {
        output.reset();
        return;
    }

    output = input;
}

}  // namespace

//
// Example of a syslog string from RFC5424:
//
// <14>1 2021-10-11T22:14:15.003Z 172.20.192.1 su - ID47 - BOM'su root' failed for lonvick on /dev/pts/8
//

namespace Orc {
namespace Log {

std::string SyslogMessage::Build() const
{
    const std::string nil(kNilValue);
    const auto timestamp = ValueOr(ToAnsiStringIso8601(*m_timestamp), nil);

    return fmt::format(
        "<{}>{} {} {} {} {} {} {} {}",
        Priority(),
        Version(),
        timestamp,
        Hostname().value_or(nil),
        ApplicationName().value_or(nil),
        ProcessId().value_or(nil),
        MessageId().value_or(nil),
        StructuredData().value_or(nil),
        Message());
}

uint8_t SyslogMessage::Priority() const
{
    return static_cast<uint8_t>(m_facility) * 8 + static_cast<uint8_t>(m_severity);
}

void SyslogMessage::SetHostname(const std::string& hostname)
{
    ::SetOptionalString(hostname, m_hostname);
}

void SyslogMessage::SetApplicationName(const std::string& app_name)
{
    ::SetOptionalString(app_name, m_app_name);
}

void SyslogMessage::SetProcessId(const std::string& procid)
{
    ::SetOptionalString(procid, m_procid);
}

void SyslogMessage::SetMessageId(const std::string& msgId)
{
    ::SetOptionalString(msgId, m_msgid);
}

}  // namespace Log
}  // namespace Orc
