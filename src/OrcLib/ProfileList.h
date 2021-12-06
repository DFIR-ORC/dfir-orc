//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "Utils/Result.h"

#include <optional>
#include <filesystem>

#pragma managed(push, off)

namespace Orc {

class Profile
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
using ProfileResult = Result<std::vector<Profile>>;

class ProfileList
{
public:
    static ProfileResult GetProfiles();

    static Result<std::wstring> DefaultProfile();
    static Result<std::filesystem::path> DefaultProfilePath();

    static Result<std::wstring> ProfilesDirectory();
    static Result<std::filesystem::path> ProfilesDirectoryPath();

    static Result<std::wstring> ProgramData();
    static Result<std::filesystem::path> ProgramDataPath();

    static Result<std::wstring> PublicProfile();
    static Result<std::filesystem::path> PublicProfilePath();
};

}  // namespace Orc
#pragma managed(pop)
