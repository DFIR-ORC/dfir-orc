//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
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
    TEST_METHOD_INITIALIZE(Initialize) {}

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(DefaultProfile)
    {
        auto default_profile = ProfileList::DefaultProfile();

        if (default_profile.has_error())
        {
            Log::Error(
                L"Failed to read DefaultProfile, test not performed (code: {:#x})", default_profile.error().value());
            return;
        }

        Assert::IsTrue(default_profile.has_value());
        Assert::IsFalse(default_profile.value().empty());

        auto default_profile_path = ProfileList::DefaultProfilePath();
        Assert::IsTrue(default_profile_path.has_value());
        auto& path = default_profile_path.value();
        Assert::IsTrue(std::filesystem::exists(path));
    }
    TEST_METHOD(ProfilesDirectory)
    {
        auto profiles_directory = ProfileList::ProfilesDirectory();

        if (profiles_directory.has_error())
        {
            Log::Error(
                L"Failed to read profiles_directory, test not performed (code: {:#x})",
                profiles_directory.error().value());
            return;
        }

        Assert::IsTrue(profiles_directory.has_value());
        Assert::IsFalse(profiles_directory.value().empty());

        auto profiles_path = ProfileList::ProfilesDirectoryPath();
        Assert::IsTrue(profiles_path.has_value());

        auto& path = profiles_path.value();
        Assert::IsTrue(std::filesystem::exists(path));
    }
    TEST_METHOD(ProgramData)
    {
        auto program_data = ProfileList::ProfilesDirectory();
        if (program_data.has_error())
        {
            Log::Error(L"Failed to read program_data, test not performed (code: {:#x})", program_data.error().value());
            return;
        }

        Assert::IsTrue(program_data.has_value());
        Assert::IsFalse(program_data.value().empty());

        auto program_data_path = ProfileList::ProfilesDirectoryPath();
        Assert::IsTrue(program_data_path.has_value());

        auto& path = program_data_path.value();
        Assert::IsTrue(std::filesystem::exists(path));
    }
    TEST_METHOD(Public)
    {
        auto public_ = ProfileList::ProfilesDirectory();
        if (public_.has_error())
        {
            Log::Error(L"Failed to read public profile, test not performed (code: {:#x})", public_.error().value());
            return;
        }

        Assert::IsTrue(public_.has_value());
        Assert::IsFalse(public_.value().empty());

        auto public_path = ProfileList::ProfilesDirectoryPath();
        Assert::IsTrue(public_path.has_value());

        auto& path = public_path.value();
        Assert::IsTrue(std::filesystem::exists(path));
    }

    TEST_METHOD(ProfileList)
    {
        auto profiles = ProfileList::GetProfiles();
        if (profiles.has_error())
        {
            Log::Error(L"Failed to read profiles, test not performed (code: {:#x})", profiles.error().value());
            return;
        }

        Assert::IsTrue(profiles.has_value());
        auto& profile_list = profiles.value();

        if (profile_list.empty())
        {
            Log::Error(L"Empty profile list, test not valid");
            return;
        }
    }
};
}  // namespace Orc::Test
