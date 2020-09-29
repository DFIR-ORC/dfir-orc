//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"
#include "ProfileList.h"

#include "Buffer.h"
#include "Registry.h"

#include <boost/scope_exit.hpp>

using namespace stx;

Orc::ProfileResult Orc::ProfileList::GetProfiles()
{
    HKEY hKey = nullptr;
    if (auto status = RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList)",
            REG_OPTION_OPEN_LINK,
            KEY_ENUMERATE_SUB_KEYS,
            &hKey);
        status != ERROR_SUCCESS)
    {
        spdlog::error(L"Failed to open registry key ProfileList (code: {:#x})", HRESULT_FROM_WIN32(status));
        return Err(HRESULT_FROM_WIN32(status));
    }
    BOOST_SCOPE_EXIT(&hKey) { RegCloseKey(hKey); }
    BOOST_SCOPE_EXIT_END;

    std::vector<Profile> retval;

    DWORD dwIndex = 0LU;
    for (;;)
    {
        auto constexpr MAX_KEY_NAME = 255LU;
        Buffer<WCHAR, MAX_KEY_NAME> keyName;
        keyName.reserve(MAX_KEY_NAME);
        DWORD keyLength = MAX_KEY_NAME;
        FILETIME lastWrite {0};

        auto status = RegEnumKeyExW(hKey, dwIndex++, keyName.get(), &keyLength, NULL, NULL, NULL,&lastWrite);

        if (status != ERROR_SUCCESS)
        {
            if (status == ERROR_NO_MORE_ITEMS)
                break;

            spdlog::error(
                L"Failed to enumerate registry key ProfileList at index {} (code: {:#x})",
                dwIndex,
                HRESULT_FROM_WIN32(status));
            return Err(HRESULT_FROM_WIN32(status));
        }
        else
            keyName.use(keyLength);

        {
            Profile profile;

            profile.ProfileKeyLastWrite = lastWrite;

            ByteBuffer sid;

            auto rSID = Registry::Read<ByteBuffer>(hKey, keyName.get(), L"Sid");
            if (rSID.is_err())
            {
                spdlog::debug(L"Failed to read SID for profile {}, using key name", keyName.get());
                PSID pSID = NULL;
                if(!ConvertStringSidToSidW(keyName.get(), &pSID))
                {
                    spdlog::debug(L"Failed profile key name {} is not a valid sid", keyName.get());
                    continue;
                }
                sid.assign((LPBYTE) pSID, GetLengthSid(pSID));
                LocalFree(pSID);
                profile.strSID.assign(keyName.get(), keyName.size());
            }
            else
            {
                sid = std::move(rSID).unwrap();

                LPWSTR pSID;
                if (!ConvertSidToStringSidW((PSID)sid.get_as<PSID>(), &pSID))
                {
                    spdlog::warn(
                        L"Failed to convert SID to a string for profile {} (code: {#x})",
                        keyName.get(),
                        HRESULT_FROM_WIN32(GetLastError()));
                    continue;
                }
                profile.strSID.assign(pSID);
                LocalFree(pSID);
            }

            
            Buffer<WCHAR,MAX_PATH> accountName;
            accountName.reserve(accountName.inner_elts());
            DWORD cchAcountName =  accountName.capacity();

            Buffer<WCHAR,MAX_PATH> domainName;
            domainName.reserve(domainName.inner_elts());
            DWORD cchDomainName = domainName.capacity();

            SID_NAME_USE SidNameUse;


            if(!LookupAccountSidW(
                NULL,
                (PSID)sid.get_as<PSID>(),
                accountName.get(),
                &cchAcountName,
                domainName.get(),
                &cchDomainName,
                &SidNameUse))
            {
                spdlog::warn(
                    L"Failed to convert SID into a username for profile {} (code: {:#x})",
                    keyName.get(),
                    HRESULT_FROM_WIN32(GetLastError()));
            }
            else
            {
                accountName.use(cchAcountName);
                profile.strUserName.emplace();
                profile.strUserName.value().assign(accountName.get(), accountName.size());

                domainName.use(cchDomainName);
                profile.strDomainName.emplace();
                profile.strDomainName.value().assign(domainName.get(), domainName.size());

                profile.SidNameUse = SidNameUse;
            }

            auto ProfileImagePath = Registry::Read<std::filesystem::path>(hKey, keyName.get(), L"ProfileImagePath");
            if (ProfileImagePath.is_err())
            {
                spdlog::warn(
                    L"Failed to read ProfileImagePath for profile {} (code: {:#x})",
                    keyName.get(),
                    std::move(ProfileImagePath).err());
                continue;
            }
            profile.ProfilePath = std::move(ProfileImagePath).unwrap();

            {
                auto LocalProfileLoadTimeHigh =
                    Registry::Read<ULONG32>(hKey, keyName.get(), L"LocalProfileLoadTimeHigh");
                auto LocalProfileLoadTimeLow = Registry::Read<ULONG32>(hKey, keyName.get(), L"LocalProfileLoadTimeLow");

                if (LocalProfileLoadTimeHigh.is_ok() && LocalProfileLoadTimeLow.is_ok())
                {
                    FILETIME ft;
                    ft.dwLowDateTime = std::move(LocalProfileLoadTimeLow).unwrap();
                    ft.dwHighDateTime = std::move(LocalProfileLoadTimeHigh).unwrap();

                    profile.LocalLoadTime = ft;
                }
            }
            {
                auto LocalProfileUnloadTimeHigh =
                    Registry::Read<ULONG32>(hKey, keyName.get(), L"LocalProfileUnloadTimeHigh");
                auto LocalProfileUnloadTimeLow =
                    Registry::Read<ULONG32>(hKey, keyName.get(), L"LocalProfileUnloadTimeLow");

                if (LocalProfileUnloadTimeHigh.is_ok() && LocalProfileUnloadTimeLow.is_ok())
                {
                    FILETIME ft;
                    ft.dwLowDateTime = std::move(LocalProfileUnloadTimeLow).unwrap();
                    ft.dwHighDateTime = std::move(LocalProfileUnloadTimeHigh).unwrap();

                    profile.LocalUnloadTime = ft;
                }
            }
            {
                auto State = Registry::Read<ULONG32>(hKey, keyName.get(), L"State");
                if (State.is_ok())
                {
                    profile.State = std::move(State).unwrap();
                }
            }
            retval.push_back(std::move(profile));
        }
    }

    return Ok(std::move(retval));
}

stx::Result<std::filesystem::path, HRESULT> Orc::ProfileList::DefaultProfilePath()
{
    return Registry::Read<std::filesystem::path>(
        HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList)", L"Default");
}

stx::Result<std::wstring, HRESULT> Orc::ProfileList::DefaultProfile()
{
    return Registry::Read<std::wstring>(
        HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList)", L"Default");
}

stx::Result<std::wstring, HRESULT> Orc::ProfileList::ProfilesDirectory()
{
    return Registry::Read<std::wstring>(
        HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList)", L"ProfilesDirectory");
}

stx::Result<std::filesystem::path, HRESULT> Orc::ProfileList::ProfilesDirectoryPath()
{
    return Registry::Read<std::filesystem::path>(
        HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList)", L"ProfilesDirectory");
}

stx::Result<std::wstring, HRESULT> Orc::ProfileList::ProgramData()
{
    return Registry::Read<std::wstring>(
        HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList)", L"ProgramData");
}

stx::Result<std::filesystem::path, HRESULT> Orc::ProfileList::ProgramDataPath()
{
    return Registry::Read<std::filesystem::path>(
        HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList)", L"ProgramData");
}

stx::Result<std::wstring, HRESULT> Orc::ProfileList::PublicProfile()
{
    return Registry::Read<std::wstring>(
        HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList)", L"Public");
}

stx::Result<std::filesystem::path, HRESULT> Orc::ProfileList::PublicProfilePath()
{
    return Registry::Read<std::filesystem::path>(
        HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList)", L"Public");
}
