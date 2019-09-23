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
};
}  // namespace Orc::Test