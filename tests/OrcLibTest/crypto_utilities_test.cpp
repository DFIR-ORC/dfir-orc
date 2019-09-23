//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "LogFileWriter.h"
#include "CryptoUtilities.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(CryptoUtilitiesTest)
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

    TEST_METHOD(CryptoUtilitiesBasicTest)
    {
        {
            HCRYPTPROV hProv = NULL;
            Assert::IsTrue(S_OK == CryptoUtilities::AcquireContext(_L_, hProv));
            Assert::IsTrue(hProv != NULL);
        }
        {
            HCRYPTPROV hProv = NULL;
            Assert::IsTrue(S_OK == CryptoUtilities::AcquireContext(_L_, hProv));
            Assert::IsTrue(hProv != NULL);
        }
    }
};
}  // namespace Orc::Test