//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "FileStream.h"
#include "Authenticode.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(AuthenticodeTest)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize) {}

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(AuthenticodeBasicTest)
    {
        Authenticode authenticode;

        Authenticode::AuthenticodeData data;
        Authenticode::PE_Hashs hashs;

        auto fstream = std::make_shared<FileStream>();
        LPCWSTR szFile = LR"(c:\windows\system32\explorer.exe)";
        if (SUCCEEDED(fstream->ReadFrom(szFile)))
        {
            Assert::IsTrue(SUCCEEDED(authenticode.Verify(szFile, fstream, data)));
        }
    }
};
}  // namespace Orc::Test
