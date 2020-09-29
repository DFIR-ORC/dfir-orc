//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "SystemDetails.h"
#include "ProfileList.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(ProfileTest)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize)
    {
    }

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(DefaultProfile)
    {
        auto default_profile = ProfileList::DefaultProfile();

        if (default_profile.is_err())
        {
            spdlog::error(L"Failed to read DefaultProfile, test not performed (code: {:#x})", default_profile.err_value());
            return;
        }

        Assert::IsTrue(default_profile.is_ok());
        Assert::IsFalse(default_profile.value().empty());

        auto default_profile_path = ProfileList::DefaultProfilePath();
        Assert::IsTrue(default_profile_path.is_ok());
        auto& path = default_profile_path.value();
        Assert::IsTrue(std::filesystem::exists(path));
    }
    TEST_METHOD(ProfilesDirectory)
    {
        auto profiles_directory = ProfileList::ProfilesDirectory();

        if (profiles_directory.is_err())
        {
            spdlog::error(L"Failed to read profiles_directory, test not performed (code: {:#x})", profiles_directory.err_value());
            return;
        }

        Assert::IsTrue(profiles_directory.is_ok());
        Assert::IsFalse(profiles_directory.value().empty());

        auto profiles_path = ProfileList::ProfilesDirectoryPath();
        Assert::IsTrue(profiles_path.is_ok());

        auto& path = profiles_path.value();
        Assert::IsTrue(std::filesystem::exists(path));
    }
    TEST_METHOD(ProgramData)
    {
        auto program_data = ProfileList::ProfilesDirectory();
        if (program_data.is_err())
        {
            spdlog::error(L"Failed to read program_data, test not performed (code: {:#x})", program_data.err_value());
            return;
        }

        Assert::IsTrue(program_data.is_ok());
        Assert::IsFalse(program_data.value().empty());

        auto program_data_path = ProfileList::ProfilesDirectoryPath();
        Assert::IsTrue(program_data_path.is_ok());

        auto& path = program_data_path.value();
        Assert::IsTrue(std::filesystem::exists(path));
    }
    TEST_METHOD(Public)
    {
        auto public_ = ProfileList::ProfilesDirectory();
        if (public_.is_err())
        {
            spdlog::error(L"Failed to read public profile, test not performed", public_.err_value());
            return;
        }

        Assert::IsTrue(public_.is_ok());
        Assert::IsFalse(public_.value().empty());

        auto public_path = ProfileList::ProfilesDirectoryPath();
        Assert::IsTrue(public_path.is_ok());

        auto& path = public_path.value();
        Assert::IsTrue(std::filesystem::exists(path));
    }

    TEST_METHOD(ProfileList)
    {
        auto profiles = ProfileList::GetProfiles();
        if (profiles.is_err())
        {
            spdlog::error(L"Failed to read profiles, test not performed (code: {:#x})", profiles.err_value());
            return;
        }

        Assert::IsTrue(profiles.is_ok());
        auto& profile_list = profiles.value();

        if (profile_list.empty())
        {
            spdlog::error(L"Empty profile list, test not valid");
            return;
        }
    }
};
}  // namespace Orc::Test
