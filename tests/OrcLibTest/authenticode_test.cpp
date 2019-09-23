//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "LogFileWriter.h"

#include "FileStream.h"
#include "Authenticode.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(AuthenticodeTest)
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

    TEST_METHOD(AuthenticodeBasicTest)
    {

        Authenticode authenticode(_L_);

        Authenticode::AuthenticodeData data;
        Authenticode::PE_Hashs hashs;

        auto fstream = std::make_shared<FileStream>(_L_);
        LPCWSTR szFile = LR"(c:\windows\system32\mrt.exe)";
        if (SUCCEEDED(fstream->ReadFrom(szFile)))
        {
            Assert::IsTrue(SUCCEEDED(authenticode.Verify(LR"(c:\windows\system32\mrt.exe)", fstream, data)));
        }
    }
};
}  // namespace Orc::Test
