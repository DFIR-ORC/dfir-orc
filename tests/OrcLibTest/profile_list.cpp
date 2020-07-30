//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "LogFileWriter.h"
#include "SystemDetails.h"
#include "ProfileList.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(ProfileTest)
{
private:
    logger _L_;
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize)
    {
        _L_ = std::make_shared<LogFileWriter>();
        helper.InitLogFileWriter(_L_);
    }

    TEST_METHOD_CLEANUP(Finalize) { helper.FinalizeLogFileWriter(_L_); }

    TEST_METHOD(DefaultProfile)
    {
        auto default_profile = ProfileList::DefaultProfile(_L_);

        if (default_profile.is_err())
        {
            log::Error(_L_, default_profile.err_value(), L"Failed to read DefaultProfile, test not performed\r\n");
            return;
        }

        Assert::IsTrue(default_profile.is_ok());
        Assert::IsFalse(default_profile.value().empty());

        auto default_profile_path = ProfileList::DefaultProfilePath(_L_);
        Assert::IsTrue(default_profile_path.is_ok());
        auto& path = default_profile_path.value();
        Assert::IsTrue(std::filesystem::exists(path));
    }
    TEST_METHOD(ProfilesDirectory)
    {
        auto profiles_directory = ProfileList::ProfilesDirectory(_L_);

        if (profiles_directory.is_err())
        {
            log::Error(
                _L_, profiles_directory.err_value(), L"Failed to read profiles_directory, test not performed\r\n");
            return;
        }

        Assert::IsTrue(profiles_directory.is_ok());
        Assert::IsFalse(profiles_directory.value().empty());

        auto profiles_path = ProfileList::ProfilesDirectoryPath(_L_);
        Assert::IsTrue(profiles_path.is_ok());

        auto& path = profiles_path.value();
        Assert::IsTrue(std::filesystem::exists(path));
    }
    TEST_METHOD(ProgramData)
    {
        auto program_data = ProfileList::ProfilesDirectory(_L_);
        if (program_data.is_err())
        {
            log::Error(_L_, program_data.err_value(), L"Failed to read program_data, test not performed\r\n");
            return;
        }

        Assert::IsTrue(program_data.is_ok());
        Assert::IsFalse(program_data.value().empty());

        auto program_data_path = ProfileList::ProfilesDirectoryPath(_L_);
        Assert::IsTrue(program_data_path.is_ok());

        auto& path = program_data_path.value();
        Assert::IsTrue(std::filesystem::exists(path));
    }
    TEST_METHOD(Public)
    {
        auto public_ = ProfileList::ProfilesDirectory(_L_);
        if (public_.is_err())
        {
            log::Error(_L_, public_.err_value(), L"Failed to read public profile, test not performed\r\n");
            return;
        }

        Assert::IsTrue(public_.is_ok());
        Assert::IsFalse(public_.value().empty());

        auto public_path = ProfileList::ProfilesDirectoryPath(_L_);
        Assert::IsTrue(public_path.is_ok());

        auto& path = public_path.value();
        Assert::IsTrue(std::filesystem::exists(path));
    }

    TEST_METHOD(ProfileList)
    {
        auto profiles = ProfileList::GetProfiles(_L_);
        if (profiles.is_err())
        {
            log::Error(_L_, profiles.err_value(), L"Failed to read profiles, test not performed\r\n");
            return;
        }

        Assert::IsTrue(profiles.is_ok());
        auto& profile_list = profiles.value();

        if (profile_list.empty())
        {
            log::Error(_L_, E_FAIL, L"Empty profile list, test not valid\r\n");
            return;
        }
    }
};
}  // namespace Orc::Test
