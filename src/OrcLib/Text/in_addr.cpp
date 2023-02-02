#include "in_addr.h"

#ifdef __has_include
#    if __has_include("ip2string.h")
#        define ENABLE_IP2STRING
#    endif
#endif

#ifdef ENABLE_IP2STRING
#include <ip2string.h>
#include <Mstcpip.h>

#include "Log/Log.h"
#include "Text/Fmt/in_addr.h"

#ifndef STATUS_SUCCESS
#    define STATUS_SUCCESS 0
#endif

namespace {

const static size_t kDefaultStringSize = 256;

}

namespace Orc {

Result<std::string> ToString(const in_addr& ip)
{
    std::string s;
    s.reserve(kDefaultStringSize);

    ULONG size = s.size();
    auto status = RtlIpv4AddressToStringExA(&ip, 0, s.data(), &size);
    if (status != STATUS_SUCCESS)
    {
        std::error_code ec = SystemError(HRESULT_FROM_NT(status));
        Log::Error("Failed RtlIpv4AddressToStringExA [{}]", ec);
        return ec;
    }

    s.resize(size);
    return s;
}

Result<std::wstring> ToWString(const in_addr& ip)
{
    std::wstring s;
    s.reserve(kDefaultStringSize);

    ULONG size = s.size();
    auto status = RtlIpv4AddressToStringExW(&ip, 0, s.data(), &size);
    if (status != STATUS_SUCCESS)
    {
        std::error_code ec = SystemError(HRESULT_FROM_NT(status));
        Log::Error("Failed RtlIpv4AddressToStringExW [{}]", ec);
        return ec;
    }

    s.resize(size);
    return s;
}

Result<std::string> ToString(const in6_addr& ip)
{
    std::string s;
    s.reserve(kDefaultStringSize);

    ULONG size = s.size();
    auto status = RtlIpv6AddressToStringExA(&ip, 0, 0, s.data(), &size);
    if (status != STATUS_SUCCESS)
    {
        std::error_code ec = SystemError(HRESULT_FROM_NT(status));
        Log::Error("Failed RtlIpv6AddressToStringExA [{}]", ec);
        return ec;
    }

    s.resize(size);
    return s;
}

Result<std::wstring> ToWString(const in6_addr& ip)
{
    std::wstring s;
    s.reserve(kDefaultStringSize);

    ULONG size = s.size();
    auto status = RtlIpv6AddressToStringExW(&ip, 0, 0, s.data(), &size);
    if (status != STATUS_SUCCESS)
    {
        std::error_code ec = SystemError(HRESULT_FROM_NT(status));
        Log::Error("Failed RtlIpv6AddressToStringExW [{}]", ec);
        return ec;
    }

    s.resize(size);
    return s;
}

}  // namespace Orc

#endif  // ENABLE_IP2STRING
