//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "OrcResult.h"

#include <optional>
#include <filesystem>

#pragma managed(push, off)

namespace Orc {

class LogFileWriter;

class ORCLIB_API Profile
{
public:
    // Profile's associated SID
    std::wstring strSID;

    // SID resolved to actual user name (if any)
    std::optional<std::wstring> strUserName;
    std::optional<std::wstring> strDomainName;
    std::optional<SID_NAME_USE> SidNameUse;

    // profile path (env vars _not_ expanded)
    std::wstring strProfilePath;

    // expanded profile path
    std::filesystem::path ProfilePath;

    // State: indicates the state of the local profile cache.
    //
    // 0001       Profile is mandatory.
    // 0002       Update the locally cached profile.
    // 0004       New local profile.
    // 0008       New central profile.
    // 0010       Update the central profile.
    // 0020       Delete the cached profile.
    // 0040       Upgrade the profile.
    // 0080       Using Guest user profile.
    // 0100       Using Administrator profile.
    // 0200       Default net profile is available and ready.
    // 0400       Slow network link identified.
    // 0800       Temporary profile loaded.
    DWORD State = 0LU;


    std::optional<FILETIME> LocalLoadTime;
    std::optional<FILETIME> LocalUnloadTime;
    FILETIME ProfileKeyLastWrite;

};
using ProfileResult = Orc::Result<std::vector<Profile>, HRESULT>;

class ORCLIB_API ProfileList
{
public:

    static ProfileResult GetProfiles(const logger& pLog);

    static Result<std::wstring> DefaultProfile(const logger& pLog);
    static Result<std::filesystem::path> DefaultProfilePath(const logger& pLog);
    
    static Result<std::wstring> ProfilesDirectory(const logger& pLog);
    static Result<std::filesystem::path> ProfilesDirectoryPath(const logger& pLog);

    static Result<std::wstring> ProgramData(const logger& pLog);
    static Result<std::filesystem::path> ProgramDataPath(const logger& pLog);

    static Result<std::wstring> PublicProfile(const logger& pLog);
    static Result<std::filesystem::path> PublicProfilePath(const logger& pLog);


};

}  // namespace Orc
#pragma managed(pop)
