//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "CryptoUtilities.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(CryptoUtilitiesTest)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize)
    {
    }

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(CryptoUtilitiesBasicTest)
    {
        {
            HCRYPTPROV hProv = NULL;
            Assert::IsTrue(S_OK == CryptoUtilities::AcquireContext(hProv));
            Assert::IsTrue(hProv != NULL);
        }
        {
            HCRYPTPROV hProv = NULL;
            Assert::IsTrue(S_OK == CryptoUtilities::AcquireContext(hProv));
            Assert::IsTrue(hProv != NULL);
        }
    }
};
}  // namespace Orc::Test
