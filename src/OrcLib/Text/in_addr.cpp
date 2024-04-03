//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): jeanga (ANSSI), fabienfl (ANSSI)
//

#include "in_addr.h"

namespace {

const static size_t kDefaultStringSize = 256;

Orc::Result<void>
RtlIpv4AddressToStringExAApi(const in_addr* Address, USHORT Port, PSTR AddressString, PULONG AddressStringLength)
{
    using FnRtlIpv4AddressToStringExA = NTSTATUS(__stdcall*)(const in_addr*, USHORT, PSTR, PULONG);

    static FnRtlIpv4AddressToStringExA fn = nullptr;
    if (fn == nullptr)
    {
        fn = reinterpret_cast<FnRtlIpv4AddressToStringExA>(
            ::GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlIpv4AddressToStringExA"));

        if (!fn)
        {
            return Orc::LastWin32Error();
        }
    }

    auto status = fn(Address, Port, AddressString, AddressStringLength);
    if (!NT_SUCCESS(status))
    {
        return Orc::NtError(status);
    }

    return Orc::Success<void>();
}

Orc::Result<void>
RtlIpv4AddressToStringExWApi(const in_addr* Address, USHORT Port, PWSTR AddressString, PULONG AddressStringLength)
{
    using FnRtlIpv4AddressToStringExW = NTSTATUS(__stdcall*)(const in_addr*, USHORT, PWSTR, PULONG);

    static FnRtlIpv4AddressToStringExW fn = nullptr;
    if (fn == nullptr)
    {
        fn = reinterpret_cast<FnRtlIpv4AddressToStringExW>(
            ::GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlIpv4AddressToStringExW"));

        if (!fn)
        {
            return Orc::LastWin32Error();
        }
    }

    auto status = fn(Address, Port, AddressString, AddressStringLength);
    if (!NT_SUCCESS(status))
    {
        return Orc::NtError(status);
    }

    return Orc::Success<void>();
}

Orc::Result<void> RtlIpv6AddressToStringExAApi(
    const in6_addr* Address,
    ULONG ScopeId,
    USHORT Port,
    PSTR AddressString,
    PULONG AddressStringLength)
{
    using FnRtlIpv6AddressToStringExA = NTSTATUS(__stdcall*)(const in6_addr*, ULONG, USHORT, PSTR, PULONG);

    static FnRtlIpv6AddressToStringExA fn = nullptr;
    if (fn == nullptr)
    {
        fn = reinterpret_cast<FnRtlIpv6AddressToStringExA>(
            ::GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlIpv6AddressToStringExA"));

        if (!fn)
        {
            return Orc::LastWin32Error();
        }
    }

    auto status = fn(Address, ScopeId, Port, AddressString, AddressStringLength);
    if (!NT_SUCCESS(status))
    {
        return Orc::NtError(status);
    }

    return Orc::Success<void>();
}

Orc::Result<void> RtlIpv6AddressToStringExWApi(
    const in6_addr* Address,
    ULONG ScopeId,
    USHORT Port,
    PWSTR AddressString,
    PULONG AddressStringLength)
{
    using FnRtlIpv6AddressToStringExW = NTSTATUS(__stdcall*)(const in6_addr*, ULONG, USHORT, PWSTR, PULONG);

    static FnRtlIpv6AddressToStringExW fn = nullptr;
    if (fn == nullptr)
    {
        fn = reinterpret_cast<FnRtlIpv6AddressToStringExW>(
            ::GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlIpv6AddressToStringExW"));

        if (!fn)
        {
            return Orc::LastWin32Error();
        }
    }

    auto status = fn(Address, ScopeId, Port, AddressString, AddressStringLength);
    if (!NT_SUCCESS(status))
    {
        return Orc::NtError(status);
    }

    return Orc::Success<void>();
}

}  // namespace

namespace Orc {

Result<std::string> ToString(const in_addr& ip)
{
    std::string s;
    s.reserve(kDefaultStringSize);

    ULONG size = s.size();
    auto rv = ::RtlIpv4AddressToStringExAApi(&ip, 0, s.data(), &size);
    if (rv.has_error())
    {
        Log::Error("Failed RtlIpv4AddressToStringExA [{}]", rv.error().value());
        return rv.error();
    }

    s.resize(size);
    return s;
}

Result<std::wstring> ToWString(const in_addr& ip)
{
    std::wstring s;
    s.reserve(kDefaultStringSize);

    ULONG size = s.size();
    auto rv = ::RtlIpv4AddressToStringExWApi(&ip, 0, s.data(), &size);
    if (rv.has_error())
    {
        Log::Error("Failed RtlIpv4AddressToStringExW [{}]", rv.error().value());
        return rv.error();
    }

    s.resize(size);
    return s;
}

Result<std::string> ToString(const in6_addr& ip)
{
    std::string s;
    s.reserve(kDefaultStringSize);

    ULONG size = s.size();
    auto rv = ::RtlIpv6AddressToStringExAApi(&ip, 0, 0, s.data(), &size);
    if (rv.has_error())
    {
        Log::Error("Failed RtlIpv6AddressToStringExA [{}]", rv.error().value());
        return rv.error();
    }

    s.resize(size);
    return s;
}

Result<std::wstring> ToWString(const in6_addr& ip)
{
    std::wstring s;
    s.reserve(kDefaultStringSize);

    ULONG size = s.size();
    auto rv = ::RtlIpv6AddressToStringExWApi(&ip, 0, 0, s.data(), &size);
    if (rv.has_error())
    {
        Log::Error("Failed RtlIpv6AddressToStringExW [{}]", rv.error().value());
        return rv.error();
    }

    s.resize(size);
    return s;
}

}  // namespace Orc
