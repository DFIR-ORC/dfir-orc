//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "SystemIdentity.h"

#include <boost/scope_exit.hpp>

#include "ProfileList.h"
#include "SystemDetails.h"

#include "CpuInfo.h"
#include "Utils/Time.h"

HRESULT Orc::SystemIdentity::Write(const std::shared_ptr<StructuredOutput::IOutput>& writer, IdentityArea areas)
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
            {
                Log::Error("Failed method OperatingSystem [{}]", SystemError(hr));
            }
        }
        if (areas & IdentityArea::PhysicalDrives)
        {
            if (auto hr = PhysicalDrives(writer); FAILED(hr))
            {
                Log::Error("Failed method PhysicalDrives [{}]", SystemError(hr));
            }
        }
        if (areas & IdentityArea::MountedVolumes)
        {
            if (auto hr = MountedVolumes(writer); FAILED(hr))
            {
                Log::Error("Failed method MountedVolumes [{}]", SystemError(hr));
            }
        }
        if (areas & IdentityArea::PhysicalMemory)
        {
            if (auto hr = PhysicalMemory(writer); FAILED(hr))
            {
                Log::Error("Failed method PhysicalMemory [{}]", SystemError(hr));
            }
        }
        if (areas & IdentityArea::CPU)
        {
            if (auto hr = CPU(writer); FAILED(hr))
            {
                Log::Error("Failed method CPU [{}]", SystemError(hr));
            }
        }
        if (areas & IdentityArea::Network)
        {
            if (auto hr = Network(writer); FAILED(hr))
            {
                Log::Error("Failed method Network [{}]", SystemError(hr));
            }
        }
    }
    if (areas & IdentityArea::ProfileList)
    {
        if (auto hr = Profiles(writer); FAILED(hr))
        {
            Log::Error("Failed method Profiles [{}]", SystemError(hr));
        }
    }
    return S_OK;
}

HRESULT
Orc::SystemIdentity::CurrentUser(const std::shared_ptr<StructuredOutput::IOutput>& writer, const LPCWSTR elt)
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
Orc::SystemIdentity::CurrentProcess(const std::shared_ptr<StructuredOutput::IOutput>& writer, const LPCWSTR elt)
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

        auto environ_result = SystemDetails::GetEnvironment();
        if (environ_result.has_value())
        {
            writer->BeginCollection(L"environment");
            BOOST_SCOPE_EXIT(&writer) { writer->EndCollection(L"environment"); }
            BOOST_SCOPE_EXIT_END;

            const auto& envs = environ_result.value();
            for (const auto& env : envs)
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

HRESULT Orc::SystemIdentity::System(const std::shared_ptr<StructuredOutput::IOutput>& writer, LPCWSTR elt)
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

HRESULT Orc::SystemIdentity::OperatingSystem(const std::shared_ptr<StructuredOutput::IOutput>& writer, LPCWSTR elt)
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
        auto installDate = SystemDetails::GetInstallDateFromRegistry();
        if (installDate)
        {
            writer->WriteNamed(L"install_date", ToStringIso8601(*installDate));
        }
    }
    {
        auto installTime = SystemDetails::GetInstallTimeFromRegistry();
        if (installTime)
        {
            writer->WriteNamed(L"install_time", ToStringIso8601(*installTime));
        }
    }
    {
        auto shutdownTime = SystemDetails::GetShutdownTimeFromRegistry();
        if (shutdownTime)
        {
            ToStringIso8601(*shutdownTime);
            writer->WriteNamed(L"shutdown_time", ToStringIso8601(*shutdownTime));
        }
    }
    {
        auto tags = SystemDetails::GetSystemTags();
        writer->BeginCollection(L"tag");
        for (const auto& tag : tags)
        {
            writer->Write(tag.c_str());
        }
        writer->EndCollection(L"tag");
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
        auto qfes = SystemDetails::GetOsQFEs();
        if (qfes.has_value())
        {
            writer->BeginCollection(L"qfe");
            for (const auto& qfe : qfes.value())
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

HRESULT Orc::SystemIdentity::Network(const std::shared_ptr<StructuredOutput::IOutput>& writer, LPCWSTR elt)
{
    writer->BeginElement(elt);
    BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndElement(elt); }
    BOOST_SCOPE_EXIT_END;
    {
        if (auto result = SystemDetails::GetNetworkAdapters(); result.has_value())
        {
            writer->BeginCollection(L"adapter");
            BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndCollection(L"adapter"); }
            BOOST_SCOPE_EXIT_END;

            auto adapters = result.value();
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
                    writer->WriteNamed(L"dns_suffix", adapter.DNSSuffix.c_str());
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

HRESULT Orc::SystemIdentity::PhysicalDrives(const std::shared_ptr<StructuredOutput::IOutput>& writer, const LPCWSTR elt)
{
    writer->BeginCollection(elt);
    BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndCollection(elt); }
    BOOST_SCOPE_EXIT_END;

    auto result = SystemDetails::GetPhysicalDrives();
    if (result.has_error())
    {
        assert(result.error().category() == std::system_category());
        return result.error().value();
    }

    auto drives = result.value();
    for (const auto& drive : drives)
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
            writer->WriteNamed(L"error_code", (ULONG32)drive.ConfigManagerErrorCode.value());
        if (drive.Status)
            writer->WriteNamed(L"status", drive.Status.value().c_str());
    }
    return S_OK;
}

HRESULT Orc::SystemIdentity::MountedVolumes(const std::shared_ptr<StructuredOutput::IOutput>& writer, const LPCWSTR elt)
{
    writer->BeginCollection(elt);
    BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndCollection(elt); }
    BOOST_SCOPE_EXIT_END;

    auto result = SystemDetails::GetMountedVolumes();
    if (result.has_error())
    {
        assert(result.error().category() == std::system_category());
        return result.error().value();
    }

    const auto& volumes = result.value();
    for (const auto& volume : volumes)
    {
        writer->BeginElement(nullptr);
        BOOST_SCOPE_EXIT(&writer) { writer->EndElement(nullptr); }
        BOOST_SCOPE_EXIT_END;

        FlagsDefinition DriveTypeDefinitions[] = {
            {SystemDetails::DriveType::Drive_Unknown, L"Unknown", L"Unknown"},
            {SystemDetails::DriveType::Drive_No_Root_Dir, L"Invalid", L"Invalid"},
            {SystemDetails::DriveType::Drive_Removable, L"Removeable", L"Removeable"},
            {SystemDetails::DriveType::Drive_Fixed, L"Fixed", L"Fixed"},
            {SystemDetails::DriveType::Drive_Remote, L"Remote", L"Remote"},
            {SystemDetails::DriveType::Drive_CDRom, L"CDROM", L"Drive_CDRom"},
            {SystemDetails::DriveType::Drive_RamDisk, L"RAMDisk", L"RAMDisk"},
            {0xFFFFFFFF, NULL, NULL}};

        writer->WriteNamed(L"path", volume.Path.c_str());
        writer->WriteNamed(L"label", volume.Label.c_str());
        writer->WriteNamed(L"serial", (ULONG32)volume.SerialNumber);
        writer->WriteNamed(L"file_system", volume.FileSystem.c_str());
        writer->WriteNamed(L"device_id", volume.DeviceId.c_str());
        writer->WriteNamed(L"is_boot", volume.bBoot);
        writer->WriteNamed(L"is_system", volume.bSystem);
        writer->WriteNamed(L"size", volume.Size);
        writer->WriteNamed(L"freespace", volume.FreeSpace);
        writer->WriteNamed(L"type", (DWORD)volume.Type, DriveTypeDefinitions);
    }
    return S_OK;
}

HRESULT Orc::SystemIdentity::PhysicalMemory(const std::shared_ptr<StructuredOutput::IOutput>& writer, const LPCWSTR elt)
{
    writer->BeginElement(elt);
    BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndElement(elt); }
    BOOST_SCOPE_EXIT_END;

    auto result = SystemDetails::GetPhysicalMemory();
    if (result.has_error())
    {
        assert(result.error().category() == std::system_category());
        return result.error().value();
    }

    const auto& mem = result.value();

    writer->WriteNamed(L"current_load", (ULONG32)mem.dwMemoryLoad);
    writer->WriteNamed(L"physical", mem.ullTotalPhys);
    writer->WriteNamed(L"pagefile", mem.ullTotalPageFile);
    writer->WriteNamed(L"available_physical", mem.ullAvailPhys);
    writer->WriteNamed(L"available_pagefile", mem.ullAvailPageFile);
    return S_OK;
}

HRESULT Orc::SystemIdentity::CPU(const std::shared_ptr<StructuredOutput::IOutput>& writer, const LPCWSTR elt)
{
    writer->BeginCollection(elt);
    BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndCollection(elt); }
    BOOST_SCOPE_EXIT_END;

    writer->BeginElement(nullptr);
    BOOST_SCOPE_EXIT(&writer) { writer->EndElement(nullptr); }
    BOOST_SCOPE_EXIT_END;

    CpuInfo cpuInfo;
    writer->WriteNamed(L"manufacturer", cpuInfo.Manufacturer());
    writer->WriteNamed(L"name", cpuInfo.Name());
    writer->WriteNamed(L"physical_processors", cpuInfo.PhysicalCores());
    writer->WriteNamed(L"logical_processors", cpuInfo.LogicalCores());
    writer->WriteNamed(L"hyperthreading", cpuInfo.IsHyperThreadingEnabled());

    return S_OK;
}

HRESULT Orc::SystemIdentity::Profiles(const std::shared_ptr<StructuredOutput::IOutput>& writer, LPCWSTR elt)
{
    writer->BeginElement(elt);
    BOOST_SCOPE_EXIT(&writer, &elt) { writer->EndElement(elt); }
    BOOST_SCOPE_EXIT_END;
    {
        auto default_profile = ProfileList::DefaultProfilePath();
        if (default_profile)
            writer->WriteNamed(L"default_profile", default_profile.value().c_str());

        auto profiles_dir = ProfileList::ProfilesDirectoryPath();
        if (profiles_dir)
            writer->WriteNamed(L"profiles_directory", profiles_dir.value().c_str());

        auto program_data = ProfileList::ProgramDataPath();
        if (program_data)
            writer->WriteNamed(L"program_data", program_data.value().c_str());

        auto public_path = ProfileList::PublicProfilePath();
        if (public_path)
            writer->WriteNamed(L"public_path", public_path.value().c_str());

        auto profiles = ProfileList::GetProfiles();
        if (profiles)
        {
            writer->BeginCollection(L"profile");
            BOOST_SCOPE_EXIT(&writer) { writer->EndCollection(L"profile"); }
            BOOST_SCOPE_EXIT_END;

            const auto& profile_list = profiles.value();
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
