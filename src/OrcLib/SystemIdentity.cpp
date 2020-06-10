//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "SystemIdentity.h"
#include "ProfileList.h"

#include "SystemDetails.h"
#include "LogFileWriter.h"

#include "boost/scope_exit.hpp"

HRESULT Orc::SystemIdentity::Write(const std::shared_ptr<StructuredOutput::IWriter>& writer, IdentityArea areas)
{
    if (areas & IdentityArea::Process)
    {
        if (auto hr = CurrentProcess(writer); FAILED(hr))
            return hr;
    }

    if (areas & IdentityArea::System)
    {
        if (auto hr = System(writer); FAILED(hr))
            return hr;
    }
    else
    {
        writer->BeginElement(L"system");
        BOOST_SCOPE_EXIT(&writer) { writer->EndElement(L"system"); }
        BOOST_SCOPE_EXIT_END;

        if (areas & IdentityArea::OperatingSystem)
        {
            if (auto hr = OperatingSystem(writer); FAILED(hr))
                return hr;
        }
        if (areas & IdentityArea::PhysicalDrives)
        {
            if (auto hr = PhysicalDrives(writer); FAILED(hr))
                return hr;
        }
        if (areas & IdentityArea::MountedVolumes)
        {
            if (auto hr = MountedVolumes(writer); FAILED(hr))
                return hr;
        }
        if (areas & IdentityArea::PhysicalMemory)
        {
            if (auto hr = PhysicalMemory(writer); FAILED(hr))
                return hr;
        }
        if (areas & IdentityArea::CPU)
        {
            if (auto hr = CPU(writer); FAILED(hr))
                return hr;
        }
        if (areas & IdentityArea::Network)
        {
            if (auto hr = Network(writer); FAILED(hr))
                return hr;
        }
    }
    if (areas & IdentityArea::ProfileList)
    {
        if (auto hr = Profiles(writer); FAILED(hr))
            return hr;
    }
    return S_OK;
}

HRESULT
Orc::SystemIdentity::CurrentUser(const std::shared_ptr<StructuredOutput::IWriter>& writer, const LPCWSTR elt)
{
    writer->BeginElement(elt);
    BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndElement(elt); }
    BOOST_SCOPE_EXIT_END;
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
    return S_OK;
}

HRESULT
    Orc::SystemIdentity::CurrentProcess(const std::shared_ptr<StructuredOutput::IWriter>& writer, const LPCWSTR elt)
{
    writer->BeginElement(elt);
    BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndElement(elt); }
    BOOST_SCOPE_EXIT_END;
    {
        std::wstring strProcessBinary;
        SystemDetails::GetProcessBinary(strProcessBinary);
        writer->WriteNamed(L"binary", strProcessBinary.c_str());

        writer->WriteNamed(L"syswow64", SystemDetails::IsWOW64());

        writer->WriteNamed(L"command_line", GetCommandLine());

        CurrentUser(writer);

        auto environ_result = SystemDetails::GetEnvironment(nullptr);
        if (environ_result.is_ok())
        {
            writer->BeginCollection(L"environment");
            BOOST_SCOPE_EXIT(&writer) { writer->EndCollection(L"environment"); }
            BOOST_SCOPE_EXIT_END;

            auto envs = environ_result.unwrap();
            for (const auto& env:envs)
            {
                writer->BeginElement(nullptr);
                BOOST_SCOPE_EXIT(&writer) { writer->EndElement(nullptr); }
                BOOST_SCOPE_EXIT_END;
                writer->WriteNamed(L"Name", env.Name.c_str());
                writer->WriteNamed(L"Value", env.Value.c_str());
            }
        }
    }
    return S_OK;
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
    if (auto hr = PhysicalDrives(writer); FAILED(hr))
        return hr;
    if (auto hr = MountedVolumes(writer); FAILED(hr))
        return hr;
    if (auto hr = PhysicalMemory(writer); FAILED(hr))
        return hr;
    if (auto hr = CPU(writer); FAILED(hr))
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
    {
        auto qfes = SystemDetails::GetOsQFEs(nullptr);
        if (qfes.is_ok())
        {
            writer->BeginCollection(L"qfe");
            for (const auto& qfe : qfes.unwrap())
            {
                writer->BeginElement(nullptr);
                {
                    writer->WriteNamed(L"hotfix_id", qfe.HotFixId.c_str());
                    writer->WriteNamed(L"installed_on", qfe.InstallDate.c_str());
                }
                writer->EndElement(nullptr);
            }
            writer->EndCollection(L"qfe");
        }
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
            BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndCollection(L"adapters"); }
            BOOST_SCOPE_EXIT_END;

            for (const auto& adapter : adapters)
            {
                writer->BeginElement(nullptr);
                BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndElement(nullptr); }
                BOOST_SCOPE_EXIT_END;

                {
                    writer->WriteNamed(L"name", adapter.Name.c_str());
                    writer->WriteNamed(L"friendly_name", adapter.FriendlyName.c_str());
                    writer->WriteNamed(L"description", adapter.Description.c_str());
                    writer->WriteNamed(L"physical", adapter.PhysicalAddress.c_str());

                    {

                        writer->BeginCollection(L"address");
                        BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndCollection(L"address"); }
                        BOOST_SCOPE_EXIT_END;
                        for (const auto& address : adapter.Addresses)
                        {
                            if (address.Mode == SystemDetails::AddressMode::MultiCast)
                                continue;

                            writer->BeginElement(nullptr);
                            BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndElement(nullptr); }
                            BOOST_SCOPE_EXIT_END;

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
                        }
                    }

                    writer->WriteNamed(L"dns_suffix", adapter.DNSSuffix.c_str());

                    {
                        writer->BeginCollection(L"dns_server");
                        BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndCollection(L"dns_server"); }
                        BOOST_SCOPE_EXIT_END;

                        for (const auto& dns : adapter.DNS)
                        {
                            writer->BeginElement(nullptr);
                            BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndElement(nullptr); }
                            BOOST_SCOPE_EXIT_END;

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
                        }
                    }
                }
            }
        }
    }
    return S_OK;
}

HRESULT Orc::SystemIdentity::PhysicalDrives(const std::shared_ptr<StructuredOutput::IWriter>& writer, const LPCWSTR elt)
{
    writer->BeginCollection(elt);
    BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndCollection(elt); }
    BOOST_SCOPE_EXIT_END;

    auto result = SystemDetails::GetPhysicalDrives(nullptr);
    if (result.is_err())
        return result.err();

    auto drives = result.unwrap();
    for (const auto& drive: drives)
    {
        writer->BeginElement(nullptr);
        BOOST_SCOPE_EXIT(&writer) { writer->EndElement(nullptr); }
        BOOST_SCOPE_EXIT_END;

        writer->WriteNamed(L"path", drive.Path.c_str());
        writer->WriteNamed(L"type", drive.MediaType.c_str());
        writer->WriteNamed(L"serial", drive.SerialNumber);
        writer->WriteNamed(L"size", drive.Size);
        if (drive.Availability)
            writer->WriteNamed(L"availability", drive.Availability.value());
        if (drive.ConfigManagerErrorCode)
            writer->WriteNamed(L"error_code", (ULONG32) drive.ConfigManagerErrorCode.value());
        if (drive.Status)
            writer->WriteNamed(L"status", drive.Status.value().c_str());
    }
    return S_OK;
}

HRESULT Orc::SystemIdentity::MountedVolumes(const std::shared_ptr<StructuredOutput::IWriter>& writer, const LPCWSTR elt)
{
    return S_OK;
}

HRESULT Orc::SystemIdentity::PhysicalMemory(const std::shared_ptr<StructuredOutput::IWriter>& writer, const LPCWSTR elt)
{
    return S_OK;
}

HRESULT Orc::SystemIdentity::CPU(const std::shared_ptr<StructuredOutput::IWriter>& writer, const LPCWSTR elt)
{
    return S_OK;
}

HRESULT Orc::SystemIdentity::Profiles(const std::shared_ptr<StructuredOutput::IWriter>& writer, LPCWSTR elt)
{
    writer->BeginElement(elt);
    BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndElement(elt); }
    BOOST_SCOPE_EXIT_END;
    {
        auto default_profile = ProfileList::DefaultProfilePath(nullptr);
        if (default_profile.is_ok())
            writer->WriteNamed(L"default_profile", default_profile.unwrap().c_str());

        auto profiles_dir = ProfileList::ProfilesDirectoryPath(nullptr);
        if (profiles_dir.is_ok())
            writer->WriteNamed(L"profiles_directory", profiles_dir.unwrap().c_str());

        auto program_data = ProfileList::ProgramDataPath(nullptr);
        if (program_data.is_ok())
            writer->WriteNamed(L"program_data", program_data.unwrap().c_str());

        auto public_path = ProfileList::PublicProfilePath(nullptr);
        if (public_path.is_ok())
            writer->WriteNamed(L"public_path", public_path.unwrap().c_str());

        auto profiles = ProfileList::GetProfiles(nullptr);
        if (profiles.is_ok())
        {
            writer->BeginCollection(L"profiles");
            BOOST_SCOPE_EXIT(&writer) { writer->EndCollection(L"profiles"); }
            BOOST_SCOPE_EXIT_END;

            const auto profile_list = profiles.unwrap();
            for (const auto& profile : profile_list)
            {
                writer->BeginElement(nullptr);
                BOOST_SCOPE_EXIT(&writer) { writer->EndElement(nullptr); }
                BOOST_SCOPE_EXIT_END;

                writer->WriteNamed(L"sid", profile.strSID.c_str());
                writer->WriteNamed(L"path", profile.ProfilePath.c_str());

                if (profile.strDomainName.has_value() && profile.strUserName.has_value())
                {
                    auto full_user = fmt::format(
                        L"{}\\{}", profile.strDomainName.value().c_str(), profile.strUserName.value().c_str());
                    writer->WriteNamed(L"user", full_user.c_str());
                }
                else if (profile.strUserName.has_value())
                {
                    writer->WriteNamed(L"user", profile.strUserName.value().c_str());
                }

                if (profile.LocalLoadTime.has_value())
                    writer->WriteNamed(L"local_load_time", profile.LocalLoadTime.value());

                if (profile.LocalUnloadTime.has_value())
                    writer->WriteNamed(L"local_unload_time", profile.LocalUnloadTime.value());

                writer->WriteNamed(L"key_last_write", profile.ProfileKeyLastWrite);
            }
        }
    }

    return S_OK;
}
