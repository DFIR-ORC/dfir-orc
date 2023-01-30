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

#include "WideAnsi.h"
#include "Buffer.h"

using namespace std;

using namespace std::string_literals;

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(WideAnsi)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize)
    {
    }

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(WideToAnsi)
    {
        {
            auto [hr, result] = Orc::WideToAnsi(L"Testing string");
            Assert::IsTrue(SUCCEEDED(hr));
            Assert::AreEqual(result.c_str(), "Testing string");
        }
        {
            Buffer<CHAR, ORC_MAX_PATH> result;

            auto hr = Orc::WideToAnsi(L"Testing string", result);
            Assert::IsTrue(SUCCEEDED(hr));
            Assert::AreEqual(result.get(), "Testing string");
        }
        {
            Buffer<WCHAR, ORC_MAX_PATH> result;

            auto hr = Orc::AnsiToWide("Testing string", result);

            Assert::IsTrue(SUCCEEDED(hr));
            Assert::AreEqual(result.get(), L"Testing string");
        }
    }
};
}  // namespace Orc::Test
