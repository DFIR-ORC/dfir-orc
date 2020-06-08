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

Orc::ProfileResult Orc::ProfileList::GetProfiles(const logger& pLog)
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
        log::Error(pLog, HRESULT_FROM_WIN32(status), L"Failed to open registry key ProfileList\r\n");
        return ProfileResult(HRESULT_FROM_WIN32(status));
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

            log::Error(pLog, HRESULT_FROM_WIN32(status), L"Failed to enumerate registry key ProfileList at index %d\r\n", dwIndex);
            return ProfileResult(HRESULT_FROM_WIN32(status));
        }
        else
            keyName.use(keyLength);

        {
            Profile profile;

            profile.ProfileKeyLastWrite = lastWrite;

            ByteBuffer sid;

            auto rSID = Registry::Read<ByteBuffer>(pLog, hKey, keyName.get(), L"Sid");
            if (rSID.is_err())
            {
                log::Verbose(
                    pLog,
                    L"Failed to read SID for profile %s, using key name\r\n", keyName.get());
                PSID pSID = NULL;
                if(!ConvertStringSidToSidW(keyName.get(), &pSID))
                {
                    log::Verbose(pLog, L"Failed profile key name %s is not a valid sid\r\n", keyName.get());
                    continue;
                }
                sid.assign((LPBYTE) pSID, GetLengthSid(pSID));
                LocalFree(pSID);
                profile.strSID.assign(keyName.get(), keyName.size());
            }
            else
            {
                sid = rSID.unwrap();

                LPWSTR pSID;
                if (!ConvertSidToStringSidW((PSID)sid.get_as<PSID>(), &pSID))
                {
                    log::Warning(
                        pLog,
                        HRESULT_FROM_WIN32(GetLastError()),
                        L"Failed to convert SID to a string for profile %s\r\n",
                        keyName.get());
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
                log::Warning(
                    pLog,
                    HRESULT_FROM_WIN32(GetLastError()),
                    L"Failed to convert SID into a username for profile %s\r\n",
                    keyName.get());
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

            auto ProfileImagePath = Registry::Read<std::filesystem::path>(pLog, hKey, keyName.get(), L"ProfileImagePath");
            if (ProfileImagePath.is_err())
            {
                log::Warning(pLog, ProfileImagePath.err(), L"Failed to read ProfileImagePath for profile %s\r\n", keyName.get());
                continue;
            }
            profile.ProfilePath = ProfileImagePath.unwrap();

            {
                auto LocalProfileLoadTimeHigh =
                    Registry::Read<ULONG32>(pLog, hKey, keyName.get(), L"LocalProfileLoadTimeHigh");
                auto LocalProfileLoadTimeLow =
                    Registry::Read<ULONG32>(pLog, hKey, keyName.get(), L"LocalProfileLoadTimeLow");

                if (LocalProfileLoadTimeHigh.is_ok() && LocalProfileLoadTimeLow.is_ok())
                {
                    FILETIME ft;
                    ft.dwLowDateTime = LocalProfileLoadTimeLow.unwrap();
                    ft.dwHighDateTime = LocalProfileLoadTimeHigh.unwrap();

                    profile.LocalLoadTime = ft;
                }
            }
            {
                auto LocalProfileUnloadTimeHigh =
                    Registry::Read<ULONG32>(pLog, hKey, keyName.get(), L"LocalProfileUnloadTimeHigh");
                auto LocalProfileUnloadTimeLow =
                    Registry::Read<ULONG32>(pLog, hKey, keyName.get(), L"LocalProfileUnloadTimeLow");

                if (LocalProfileUnloadTimeHigh.is_ok() && LocalProfileUnloadTimeLow.is_ok())
                {
                    FILETIME ft;
                    ft.dwLowDateTime = LocalProfileUnloadTimeLow.unwrap();
                    ft.dwHighDateTime = LocalProfileUnloadTimeHigh.unwrap();

                    profile.LocalUnloadTime = ft;
                }
            }
            {
                auto State = Registry::Read<ULONG32>(pLog, hKey, keyName.get(), L"State");
                if (State.is_ok())
                {
                    profile.State = State.unwrap();
                }
            }
            retval.push_back(std::move(profile));
        }
    }

    return ProfileResult(std::move(retval));
}

Orc::Result<std::filesystem::path> Orc::ProfileList::DefaultProfilePath(const logger& pLog)
{
    return Registry::Read<std::filesystem::path>(
        pLog, HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList)", L"Default");
}

Orc::Result<std::wstring> Orc::ProfileList::DefaultProfile(const logger& pLog)
{
    return Registry::Read<std::wstring>(
        pLog, HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList)", L"Default");
}

Orc::Result<std::wstring> Orc::ProfileList::ProfilesDirectory(const logger& pLog)
{
    return Registry::Read<std::wstring>(
        pLog, HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList)", L"ProfilesDirectory");
}

Orc::Result<std::filesystem::path> Orc::ProfileList::ProfilesDirectoryPath(const logger& pLog)
{
    return Registry::Read<std::filesystem::path>(
        pLog, HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList)", L"ProfilesDirectory");
}

Orc::Result<std::wstring> Orc::ProfileList::ProgramData(const logger& pLog)
{
    return Registry::Read<std::wstring>(
        pLog, HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList)", L"ProgramData");
}

Orc::Result<std::filesystem::path> Orc::ProfileList::ProgramDataPath(const logger& pLog)
{
    return Registry::Read<std::filesystem::path>(
        pLog, HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList)", L"ProgramData");
}

Orc::Result<std::wstring> Orc::ProfileList::PublicProfile(const logger& pLog)
{
    return Registry::Read<std::wstring>(
        pLog, HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList)", L"Public");
}

Orc::Result<std::filesystem::path> Orc::ProfileList::PublicProfilePath(const logger& pLog)
{
    return Registry::Read<std::filesystem::path>(
        pLog, HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList)", L"Public");
}
