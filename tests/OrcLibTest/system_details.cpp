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

#include "LogFileWriter.h"

#include "SystemDetails.h"

using namespace std;

using namespace std::string_literals;

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(SystemDetailsTest)
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
        auto result = SystemDetails::GetPhysicalDrives(_L_);
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
        auto result = SystemDetails::GetMountedVolumes(_L_);
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

            Assert::IsTrue(volume.Size > 0);
            Assert::IsTrue(volume.FreeSpace > 0);
            Assert::IsFalse(volume.Path.empty());
            Assert::IsFalse(volume.DeviceId.empty());
            Assert::IsFalse(volume.FileSystem.empty());
        }
        Assert::IsTrue(bHasBoot);
        Assert::IsTrue(bHasSystem);
    }

    TEST_METHOD(QFE)
    {
        auto result = SystemDetails::GetOsQFEs(_L_);
        Assert::IsTrue(result.is_ok());

        auto qfes = move(result).unwrap();

        for (const auto& qfe : qfes)
        {
            Assert::IsFalse(qfe.HotFixId.empty());
        }
    }
    TEST_METHOD(Environment)
    {
        auto result = SystemDetails::GetEnvironment(_L_);

        Assert::IsTrue(result.is_ok());

        auto envs = move(result).unwrap();
        Assert::IsFalse(envs.empty());
        for (const auto& env : envs)
        {
            Assert::IsFalse(env.Name.empty());
        }
    }

    TEST_METHOD(CommandLine)
    {
        auto cmdLine = SystemDetails::GetCmdLine();
        auto cmdLineWMI = SystemDetails::GetCmdLine(_L_, GetCurrentProcessId());
        Assert::IsTrue(cmdLine.is_ok());
        Assert::IsTrue(cmdLineWMI.is_ok());
        Assert::AreEqual(move(cmdLine).unwrap(), move(cmdLineWMI).unwrap());

        auto parent_id = SystemDetails::GetParentProcessId(_L_);
        Assert::IsTrue(parent_id.is_ok());
        auto parentCmdLine = SystemDetails::GetCmdLine(_L_, move(parent_id).unwrap());
        Assert::IsTrue(parentCmdLine.is_ok());
        Assert::IsFalse(move(parentCmdLine).unwrap().empty());
        
    }


};
}  // namespace Orc::Test
