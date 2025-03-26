//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <optional>
#include <chrono>

#include "Log/Syslog/SyslogFacility.h"
#include "Log/Syslog/SyslogLevel.h"

//
// RFC5426: Transmission of Syslog Messages over UDP
//
// https://tools.ietf.org/html/rfc5426
//
// The message MUST be formatted and truncated according to RFC 5424. Additional data MUST NOT be present in the
// datagram payload.
//

//
// RFC5424: The Syslog Protocol
//
// https://tools.ietf.org/html/rfc5424
//

//
// RFC5234: Augmented BNF for Syntax Specifications: ABNF
//
// Help with syntax used to specify values (ex: <SP> ...)
//
// https://tools.ietf.org/html/rfc5234
//

namespace Orc {
namespace Log {

class SyslogMessage
{
public:
    static constexpr std::string_view kNilValue = "-";  // RFC5424
    using TimestampT = std::chrono::time_point<std::chrono::system_clock>;

    SyslogMessage()
        : m_facility(SyslogFacility::LocalUse7)
        , m_severity(SyslogLevel::Informational)
        , m_version(1)
        , m_hostname()
    {
    }

    std::string Build() const;

    SyslogFacility Facility() const { return m_facility; }
    void SetFacility(SyslogFacility facility) { m_facility = facility; }

    SyslogLevel Severity() const { return m_severity; }
    void SetSeverity(SyslogLevel severity) { m_severity = severity; }

    uint8_t Priority() const;

    uint8_t Version() const { return m_version; }

    void SetHostname(const std::string& hostname);
    const std::optional<std::string>& Hostname() const { return m_hostname; }

    void SetApplicationName(const std::string& app_name);
    const std::optional<std::string>& ApplicationName() const { return m_app_name; }

    void SetProcessId(const std::string& procid);
    const std::optional<std::string>& ProcessId() const { return m_procid; }

    void SetMessageId(const std::string& msgid);
    const std::optional<std::string>& MessageId() const { return m_msgid; }

    const std::optional<TimestampT>& Timestamp() const { return m_timestamp; }
    void SetTimestamp(const TimestampT& timestamp) { m_timestamp = timestamp; }
    void ResetTimestamp() { m_timestamp.reset(); }

    // void SetStructuredData(const std::string& structuredData);  // Unsupported
    const std::optional<std::string>& StructuredData() const { return m_structuredData; }

    void SetMessage(const std::string& msg)
    {
        constexpr auto kBom = std::string_view("\xEF\xBB\xBF");
        m_msg = kBom;
        m_msg.append(msg);
    }

    const std::string& Message() const { return m_msg; }

private:
    SyslogFacility m_facility;
    SyslogLevel m_severity;
    uint8_t m_version;
    std::optional<std::string> m_hostname;

    std::optional<std::string> m_app_name;
    std::optional<std::string> m_procid;
    std::optional<std::string> m_msgid;

    // std::optional<std::string> timestamp;

    std::optional<TimestampT> m_timestamp;

    std::optional<std::string> m_structuredData;  // currently not supported
    std::string m_msg;
};

}  // namespace Log
}  // namespace Orc
