//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "RunningCode.h"

#include <sstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(RunningCodeTest)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize) {}

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(RunningCodeBasicTest)
    {
        Log::Info("Entering RunningCode basic test");

        RunningCode rc;

        Assert::IsTrue(SUCCEEDED(rc.EnumRunningCode()));

        Modules mods;
        Assert::IsTrue(SUCCEEDED(rc.GetUniqueModules(mods)));

        for (const auto& mod : mods)
        {
            Log::Info(L"Module '{}' in process", mod.strModule);

            std::wstringstream stream;

            for (const auto pid : mod.Pids)
            {
                stream << pid << L" ";
            }
            Log::Info(L"\t{}", stream.str());
        }
    }
};
}  // namespace Orc::Test
