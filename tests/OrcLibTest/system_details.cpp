//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include <memory>
#include <iostream>
#include <iomanip>

#include "SystemDetails.h"

using namespace std::string_literals;

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(SystemDetailsTest)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize)
    {
    }

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(OsVersion)
    {
        DWORD dwMajor1 = 0, dwMinor1 = 0;

        SystemDetails::GetOSVersion(dwMajor1, dwMinor1);

        auto [dwMajor2, dwMinor2] = SystemDetails::GetOSVersion();

        Assert::AreEqual(dwMajor1, dwMajor2, L"Major OS version mismatch");
        Assert::AreEqual(dwMinor1, dwMinor2, L"Minor OS version mismatch");
    }
    TEST_METHOD(UserNameAndSID)
    {
        std::wstring strUserName;
        std::wstring strUserSID;

        SystemDetails::WhoAmI(strUserName);
        SystemDetails::UserSID(strUserSID);

        Assert::IsFalse(strUserName.empty());
        Assert::IsFalse(strUserSID.empty());
    }
    TEST_METHOD(SystemTags)
    {
        const auto& tags = SystemDetails::GetSystemTags();

        Assert::IsFalse(tags.empty());

        for (const auto& tag : tags)
        {
            Assert::IsFalse(tag.empty());
        }
    }
    TEST_METHOD(PhysicalDrives)
    {
        auto result = SystemDetails::GetPhysicalDrives();
        Assert::IsTrue(result.has_value());
        auto drives = *result;
        Assert::IsFalse(drives.empty());

        for (const auto& drive: drives)
        {
            Assert::IsFalse(drive.Path.empty());
        }
    }
    TEST_METHOD(MountedVolumes)
    {
        auto result = SystemDetails::GetMountedVolumes();
        Assert::IsTrue(result.has_value());
        auto volumes = *result;
        Assert::IsFalse(volumes.empty());

        bool bHasSystem = false;
        bool bHasBoot = false;
        for (const auto& volume: volumes)
        {
            if (volume.bBoot)
                bHasBoot=true;
            if (volume.bSystem)
                bHasSystem=true;

            Assert::IsFalse(volume.Path.empty(), L"Volume path should not be empty");
        }
        Assert::IsTrue(bHasBoot, L"This system does not seem to have a boot volume");
        Assert::IsTrue(bHasSystem, L"This system does not seem to have a system volume");
    }

    TEST_METHOD(QFE)
    {
        auto result = SystemDetails::GetOsQFEs();
        Assert::IsTrue(result.has_value(), L"Failed to retrieve installed OS QFEs");

        auto qfes = *result;

        for (const auto& qfe : qfes)
        {
            Assert::IsFalse(qfe.HotFixId.empty(), L"HotFixId should not be null");
        }
    }
    TEST_METHOD(Environment)
    {
        auto result = SystemDetails::GetEnvironment();

        Assert::IsTrue(result.has_value());

        auto envs = *result;
        Assert::IsFalse(envs.empty());
        for (const auto& env : envs)
        {
            Assert::IsFalse(env.Name.empty());
        }
    }

    TEST_METHOD(CommandLine)
    {
        auto cmdLine = SystemDetails::GetCmdLine();
        auto cmdLineWMI = SystemDetails::GetCmdLine(GetCurrentProcessId());
        Assert::IsTrue(cmdLine.has_value());
        Assert::IsTrue(cmdLineWMI.has_value());
        Assert::AreEqual(*cmdLine, *cmdLineWMI);

        auto parent_id = SystemDetails::GetParentProcessId();
        Assert::IsTrue(parent_id.has_value());
        auto parentCmdLine = SystemDetails::GetCmdLine(*parent_id);
        Assert::IsTrue(parentCmdLine.has_value());
        Assert::IsFalse((*parentCmdLine).empty());
    }


};
}  // namespace Orc::Test
