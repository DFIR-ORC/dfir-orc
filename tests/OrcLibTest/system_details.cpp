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

    TEST_METHOD(GetOsVersionTest)
    {
        DWORD dwMajor1 = 0, dwMinor1 = 0;

        SystemDetails::GetOSVersion(dwMajor1, dwMinor1);

        auto [dwMajor2, dwMinor2] = SystemDetails::GetOSVersion();

        Assert::AreEqual(dwMajor1, dwMajor2, L"Major OS version mismatch");
        Assert::AreEqual(dwMinor1, dwMinor2, L"Minor OS version mismatch");
    }
    TEST_METHOD(GetUserNameAndSID)
    {
        std::wstring strUserName;
        std::wstring strUserSID;

        SystemDetails::WhoAmI(strUserName);
        SystemDetails::UserSID(strUserSID);

        Assert::IsFalse(strUserName.empty());
        Assert::IsFalse(strUserSID.empty());
    }
    TEST_METHOD(GetSystemTags)
    {
        const auto& tags = SystemDetails::GetSystemTags();

        Assert::IsFalse(tags.empty());

        for (const auto& tag : tags)
        {
            Assert::IsFalse(tag.empty());
        }
    }
    TEST_METHOD(GetPhysicalDrives)
    {
        auto result = SystemDetails::GetPhysicalDrives(_L_);
        Assert::IsTrue(result.is_ok());
        auto drives = result.unwrap();
        Assert::IsFalse(drives.empty());

        for (const auto& drive: drives)
        {
            Assert::IsFalse(drive.Path.empty());
        }
    }
    TEST_METHOD(GetQFEs)
    {
        auto result = SystemDetails::GetOsQFEs(_L_);
        Assert::IsTrue(result.is_ok());

        auto qfes = result.unwrap();

        for (const auto& qfe : qfes)
        {
            Assert::IsFalse(qfe.HotFixId.empty());
        }
    }
    TEST_METHOD(GetEnvironment)
    {
        auto result = SystemDetails::GetEnvironment(_L_);

        Assert::IsTrue(result.is_ok());

        auto envs = result.unwrap();
        Assert::IsFalse(envs.empty());
        for (const auto& env : envs)
        {
            Assert::IsFalse(env.Name.empty());
        }
    }

    TEST_METHOD(GetCommandLineTest)
    {
        auto cmdLine = SystemDetails::GetCmdLine();
        auto cmdLineWMI = SystemDetails::GetCmdLine(_L_, GetCurrentProcessId());
        Assert::IsTrue(cmdLine.is_ok());
        Assert::IsTrue(cmdLineWMI.is_ok());
        Assert::AreEqual(cmdLine.unwrap(), cmdLineWMI.unwrap());

        auto parent_id = SystemDetails::GetParentProcessId(_L_);
        Assert::IsTrue(parent_id.is_ok());
        auto parentCmdLine = SystemDetails::GetCmdLine(_L_, parent_id.unwrap());
        Assert::IsTrue(parentCmdLine.is_ok());
        Assert::IsFalse(parentCmdLine.unwrap().empty());
        
    }

};
}  // namespace Orc::Test
