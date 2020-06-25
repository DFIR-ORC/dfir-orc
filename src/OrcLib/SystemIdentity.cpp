//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "SystemIdentity.h"

#include "SystemDetails.h"
#include "LogFileWriter.h"

#include "boost/scope_exit.hpp"

HRESULT Orc::SystemIdentity::Write(const std::shared_ptr<StructuredOutput::IWriter>& writer, IdentityArea areas)
{
    if (areas & IdentityArea::System)
    {
        if (auto hr = System(writer); FAILED(hr))
            return hr;
    }
    else if (areas & IdentityArea::OperatingSystem)
    {
        writer->BeginElement(L"system");
        auto hr = OperatingSystem(writer);
        writer->EndElement(L"system");
        if (FAILED(hr))
            return hr;
    }
    else if (areas & IdentityArea::Network)
    {
        writer->BeginElement(L"system");
        auto hr = Network(writer);
        writer->EndElement(L"system");
        if (FAILED(hr))
            return hr;
    }
    if (areas & IdentityArea::ProfileList)
    {
        if (auto hr = ProfileList(writer); FAILED(hr))
            return hr;
    }
    return S_OK;
}

HRESULT Orc::SystemIdentity::Process(const std::shared_ptr<StructuredOutput::IWriter>& writer, const LPCWSTR elt)
{
    writer->BeginElement(L"process");
    {
        std::wstring strProcessBinary;
        SystemDetails::GetProcessBinary(strProcessBinary);
        writer->WriteNamed(L"binary", strProcessBinary.c_str());

        writer->WriteNamed(L"syswow64", SystemDetails::IsWOW64());

        writer->WriteNamed(L"command_line", GetCommandLine());


        writer->BeginElement(L"user");
        {
            std::wstring strUserName;
            SystemDetails::WhoAmI(strUserName);
            writer->WriteNamed(L"username", strUserName.c_str());

            std::wstring strUserSID;
            SystemDetails::UserSID(strUserSID);
            writer->WriteNamed(L"SID", strUserSID.c_str());

            bool bElevated = false;
            SystemDetails::AmIElevated(bElevated);
            writer->WriteNamed(L"elevated", bElevated);

            std::wstring locale;
            if (auto hr = SystemDetails::GetUserLocale(locale); SUCCEEDED(hr))
                writer->WriteNamed(L"locale", locale.c_str());

            std::wstring language;
            if (auto hr = SystemDetails::GetUserLanguage(language); SUCCEEDED(hr))
                writer->WriteNamed(L"language", language.c_str());
        }
        writer->EndElement(L"user");
    }
    writer->EndElement(L"process");
    return E_NOTIMPL;
}

HRESULT Orc::SystemIdentity::System(const std::shared_ptr<StructuredOutput::IWriter>& writer, LPCWSTR elt)
{
    writer->BeginElement(elt);
    BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndElement(elt); }
    BOOST_SCOPE_EXIT_END;

    {
        std::wstring strComputerName;
        SystemDetails::GetOrcComputerName(strComputerName);
        writer->WriteNamed(L"name", strComputerName.c_str());
    }
    {
        std::wstring strFullComputerName;
        SystemDetails::GetOrcFullComputerName(strFullComputerName);
        writer->WriteNamed(L"fullname", strFullComputerName.c_str());
    }
    {
        std::wstring strSystemType;
        SystemDetails::GetSystemType(strSystemType);
        writer->WriteNamed(L"type", strSystemType.c_str());
    }
    {
        WORD wArch = 0;
        SystemDetails::GetArchitecture(wArch);
        switch (wArch)
        {
            case PROCESSOR_ARCHITECTURE_INTEL:
                writer->WriteNamed(L"architecture", L"x86");
                break;
            case PROCESSOR_ARCHITECTURE_AMD64:
                writer->WriteNamed(L"architecture", L"x64");
                break;
            default:
                break;
        }
    }
    if (auto hr = OperatingSystem(writer); FAILED(hr))
        return hr;
    if (auto hr = Network(writer); FAILED(hr))
        return hr;
    return S_OK;
}

HRESULT Orc::SystemIdentity::OperatingSystem(const std::shared_ptr<StructuredOutput::IWriter>& writer, LPCWSTR elt)
{
    writer->BeginElement(elt);
    BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndElement(elt); }
    BOOST_SCOPE_EXIT_END;
    {
        std::wstring strDescr;
        SystemDetails::GetDescriptionString(strDescr);

        writer->WriteNamed(L"description", strDescr.c_str());
    }
    {
        auto [major, minor] = SystemDetails::GetOSVersion();
        auto version = fmt::format(L"{}.{}", major, minor);
        writer->WriteNamed(L"version", version.c_str());
    }
    {
        TIME_ZONE_INFORMATION tzi;
        ZeroMemory(&tzi, sizeof(tzi));
        if (auto active = GetTimeZoneInformation(&tzi); active != TIME_ZONE_ID_INVALID)
        {
            writer->BeginElement(L"time_zone");
            {
                writer->WriteNamed(L"daylight", tzi.DaylightName);
                writer->WriteNamed(L"daylight_bias", tzi.DaylightBias);
                writer->WriteNamed(L"standard", tzi.StandardName);
                writer->WriteNamed(L"standard_bias", tzi.StandardBias);
                writer->WriteNamed(L"current_bias", tzi.Bias);
                switch (active)
                {
                    case TIME_ZONE_ID_UNKNOWN:
                    case TIME_ZONE_ID_STANDARD:
                        writer->WriteNamed(L"current", L"standard");
                        break;
                    case TIME_ZONE_ID_DAYLIGHT:
                        writer->WriteNamed(L"current", L"daylight");
                        break;
                }
            }
            writer->EndElement(L"time_zone");
        }
    }
    {
        std::wstring locale;
        if (auto hr = SystemDetails::GetUserLocale(locale); SUCCEEDED(hr))
            writer->WriteNamed(L"locale", locale.c_str());
    }
    {
        std::wstring language;
        if (auto hr = SystemDetails::GetUserLanguage(language); SUCCEEDED(hr))
            writer->WriteNamed(L"language", language.c_str());
    }
    {
        auto tags = SystemDetails::GetSystemTags();
        writer->BeginCollection(L"tags");
        for (const auto& tag : tags)
        {
            writer->Write(tag.c_str());
        }
        writer->EndCollection(L"tags");
    }
    return S_OK;
}

HRESULT Orc::SystemIdentity::Network(const std::shared_ptr<StructuredOutput::IWriter>& writer, LPCWSTR elt)
{
    writer->BeginElement(elt);
    BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndElement(elt); }
    BOOST_SCOPE_EXIT_END;
    {
        if (const auto& [hr, adapters] = SystemDetails::GetNetworkAdapters(); SUCCEEDED(hr))
        {
            writer->BeginCollection(L"adapters");

            for (const auto& adapter : adapters)
            {
                writer->BeginElement(nullptr);
                {
                    writer->WriteNamed(L"name", adapter.Name.c_str());
                    writer->WriteNamed(L"friendly_name", adapter.FriendlyName.c_str());
                    writer->WriteNamed(L"description", adapter.Description.c_str());
                    writer->WriteNamed(L"physical", adapter.PhysicalAddress.c_str());

                    writer->BeginCollection(L"address");
                    for (const auto& address : adapter.Addresses)
                    {
                        if (address.Mode == SystemDetails::AddressMode::MultiCast)
                            continue;

                        writer->BeginElement(nullptr);

                        switch (address.Type)
                        {
                            case SystemDetails::AddressType::IPV4:
                                writer->WriteNamed(L"ipv4", address.Address.c_str());
                                break;
                            case SystemDetails::AddressType::IPV6:
                                writer->WriteNamed(L"ipv6", address.Address.c_str());
                                break;
                            default:
                                writer->WriteNamed(L"other", address.Address.c_str());
                                break;
                        }

                        switch (address.Mode)
                        {
                            case SystemDetails::AddressMode::AnyCast:
                                writer->WriteNamed(L"mode", L"anycast");
                                break;
                            case SystemDetails::AddressMode::MultiCast:
                                writer->WriteNamed(L"mode", L"multicast");
                                break;
                            case SystemDetails::AddressMode::UniCast:
                                writer->WriteNamed(L"mode", L"unicast");
                                break;
                            default:
                                writer->WriteNamed(L"mode", L"other");
                                break;
                        }
                        writer->EndElement(nullptr);
                    }
                    writer->EndCollection(L"address");

                    writer->WriteNamed(L"dns_suffix", adapter.DNSSuffix.c_str());

                    writer->BeginCollection(L"dns_server");
                    for (const auto& dns : adapter.DNS)
                    {
                        writer->BeginElement(nullptr);
                        switch (dns.Type)
                        {
                            case SystemDetails::AddressType::IPV4:
                                writer->WriteNamed(L"ipv4", dns.Address.c_str());
                                break;
                            case SystemDetails::AddressType::IPV6:
                                writer->WriteNamed(L"ipv6", dns.Address.c_str());
                                break;
                            default:
                                writer->WriteNamed(L"other", dns.Address.c_str());
                                break;
                        }
                        writer->EndElement(nullptr);
                    }
                    writer->EndCollection(L"dns_server");
                }
                writer->EndElement(nullptr);
            }
            writer->EndCollection(L"adapters");
        }
    }
    return S_OK;
}

HRESULT Orc::SystemIdentity::ProfileList(const std::shared_ptr<StructuredOutput::IWriter>& writer, LPCWSTR elt)
{
    return S_OK;
}
