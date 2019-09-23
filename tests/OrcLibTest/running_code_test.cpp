//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "LogFileWriter.h"

#include "RunningCode.h"

#include <sstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(RunningCodeTest)
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

    TEST_METHOD(RunningCodeBasicTest)
    {
        log::Info(_L_, L"Entering RunningCode basic test\r\n");

        RunningCode rc(_L_);

        Assert::IsTrue(SUCCEEDED(rc.EnumRunningCode()));

        Modules mods;
        Assert::IsTrue(SUCCEEDED(rc.GetUniqueModules(mods)));

        for (const auto& mod : mods)
        {
            log::Info(_L_, L"Module %s in process\r\n", mod.strModule.c_str());

            std::wstringstream stream;

            for (const auto pid : mod.Pids)
            {
                stream << pid << L" ";
            }
            log::Info(_L_, L"\t%s\r\n", stream.str().c_str());
        }
    }
};
}  // namespace Orc::Test
