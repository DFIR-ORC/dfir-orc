//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "SyslogSink.h"

#include "Text/Iconv.h"
#include "Utils/Guard/Winsock.h"

using namespace Orc;

namespace {

std::optional<std::string> GetHostName()
{
    std::wstring hostnameW;

    HRESULT hr = SystemDetails::GetFullComputerName(hostnameW);
    if (FAILED(hr))
    {
        return {};
    }

    std::error_code ec;
    const auto hostname = Orc::ToUtf8(hostnameW, ec);
    if (ec)
    {
        return {};
    }

    return hostname;
}

}  // namespace

namespace Orc {
namespace Log {

template class SyslogSink<std::mutex>;

template <typename T>
SyslogSink<T>::SyslogSink()
{
    const std::string nil(SyslogMessage::kNilValue);  // RFC5424
    const char eol[] = "";

    //
    // Formatter used to create a syslog message following RFC5424 like:
    //
    // <14>1 2021-10-11T22:14:15.003Z 172.20.192.1 su - ID47 - BOM'su root' failed for lonvick on /dev/pts/8
    //
    const auto pattern =
        fmt::format("<14>1 %Y-%m-%dT%T.%eZ {} DFIR-Orc %P {} {} \xef\xbb\xbf%v", GetHostName().value_or(nil), nil, nil);

    spdlog::sinks::base_sink<T>::set_formatter_(
        std::make_unique<spdlog::pattern_formatter>(pattern, spdlog::pattern_time_type::utc, eol));
}

template <typename T>
void SyslogSink<T>::AddEndpoint(UdpSocket&& socket)
{
    m_syslog.AddEndpoint(std::move(socket));
}

template <typename T>
void SyslogSink<T>::AddEndpoint(const std::string& host, const std::string& port, std::error_code& ec)
{
    m_syslog.AddEndpoint(host, port, ec);
}

template <typename T>
void SyslogSink<T>::sink_it_(const spdlog::details::log_msg& msg)
{
    spdlog::memory_buf_t payload;
    spdlog::sinks::base_sink<T>::formatter_->format(msg, payload);

    std::error_code ec;
    m_syslog.Send(std::string_view(payload.data(), payload.size()), ec);
    if (ec)
    {
        assert(0);  // Do not trigger any log as it create a recursive loop
    }
}

}  // namespace Log
}  // namespace Orc
