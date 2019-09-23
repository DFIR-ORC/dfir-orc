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
    logger _L_;
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize)
    {
        _L_ = std::make_shared<LogFileWriter>();
        helper.InitLogFileWriter(_L_);
    }

    TEST_METHOD_CLEANUP(Finalize) { helper.FinalizeLogFileWriter(_L_); }

    TEST_METHOD(WideToAnsi)
    {
        {
            auto [hr, result] = Orc::WideToAnsi(_L_, L"Testing string");
            Assert::IsTrue(SUCCEEDED(hr));
            Assert::AreEqual(result.c_str(), "Testing string");
        }
        {
            Buffer<CHAR, MAX_PATH> result;

            auto hr = Orc::WideToAnsi(_L_, L"Testing string", result);
            Assert::IsTrue(SUCCEEDED(hr));
            Assert::AreEqual(result.get(), "Testing string");
        }
        {
            Buffer<WCHAR, MAX_PATH> result;

            auto hr = Orc::AnsiToWide(_L_, "Testing string", result);

            Assert::IsTrue(SUCCEEDED(hr));
            Assert::AreEqual(result.get(), L"Testing string");
        }
    }
};
}  // namespace Orc::Test