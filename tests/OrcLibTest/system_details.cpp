//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
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
        Assert::IsTrue(result.is_ok());
        auto drives = move(result).unwrap();
        Assert::IsFalse(drives.empty());

        for (const auto& drive: drives)
        {
            Assert::IsFalse(drive.Path.empty());
        }
    }
    TEST_METHOD(MountedVolumes)
    {
        auto result = SystemDetails::GetMountedVolumes();
        Assert::IsTrue(result.is_ok());
        auto volumes = move(result).unwrap();
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
        Assert::IsTrue(result.is_ok(), L"Failed to retrieve installed OS QFEs");

        auto qfes = move(result).unwrap();

        for (const auto& qfe : qfes)
        {
            Assert::IsFalse(qfe.HotFixId.empty(), L"HotFixId should not be null");
        }
    }
    TEST_METHOD(Environment)
    {
        auto result = SystemDetails::GetEnvironment();

        Assert::IsTrue(result.is_ok());

        auto envs = std::move(result).unwrap();
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
        Assert::IsTrue(cmdLine.is_ok());
        Assert::IsTrue(cmdLineWMI.is_ok());
        Assert::AreEqual(std::move(cmdLine).unwrap(), std::move(cmdLineWMI).unwrap());

        auto parent_id = SystemDetails::GetParentProcessId();
        Assert::IsTrue(parent_id.is_ok());
        auto parentCmdLine = SystemDetails::GetCmdLine(std::move(parent_id).unwrap());
        Assert::IsTrue(parentCmdLine.is_ok());
        Assert::IsFalse(std::move(parentCmdLine).unwrap().empty());
        
    }


};
}  // namespace Orc::Test
