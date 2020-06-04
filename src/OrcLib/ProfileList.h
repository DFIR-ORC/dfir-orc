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
    std::optional<std::wstring> strName;

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
    DWORD State;
};

class ORCLIB_API ProfileList
{
public:
    static std::vector<Profile> GetProfiles(const logger& pLog);

    static PathResult DefaultProfile(const logger& pLog);

    std::filesystem::path DefaultProfilePath(const logger& pLog);
    std::wstring ProfilesDirectory(const logger& pLog);
    std::filesystem::path ProfilesDirectoryPath(const logger& pLog);
    std::wstring ProgramData(const logger& pLog);
    std::filesystem::path ProgramDataPath(const logger& pLog);
    std::wstring PublicProfile(const logger& pLog);
    std::filesystem::path PublicProfilePath(const logger& pLog);
};

}  // namespace Orc
#pragma managed(pop)
