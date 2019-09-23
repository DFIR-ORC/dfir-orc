//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "LogFileWriter.h"
#include "XORStream.h"
#include "MemoryStream.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(XORStreamTest)
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

    TEST_METHOD(XORStreamBasicTest)
    {
        unsigned char data[5] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};

        auto pMemStream = std::make_shared<MemoryStream>(_L_);
        Assert::IsTrue(S_OK == pMemStream->OpenForReadWrite(sizeof(data)));

        ULONGLONG ullDataWritten = 0LL;
        Assert::IsTrue(S_OK == pMemStream->CanWrite());
        Assert::IsTrue(S_OK == pMemStream->Write(data, sizeof(data), &ullDataWritten));
        Assert::IsTrue(ullDataWritten == 5);
        Assert::IsTrue(!memcmp(data, pMemStream->GetConstBuffer().GetData(), sizeof(data)));

        DWORD xorPattern = 0xDDCCBBAA;
        auto pXORStream = std::make_shared<XORStream>(_L_);

        Assert::IsTrue(S_OK == pXORStream->SetXORPattern(xorPattern));
        Assert::IsTrue(S_OK == pXORStream->OpenForXOR(pMemStream));

        unsigned char expectedResult[sizeof(data)] = {0x0, 0x0, 0x0, 0x0, 0x44};
        ULONGLONG ullDataRead = 0LL;
        Assert::IsTrue(S_OK == pXORStream->CanSeek());
        Assert::IsTrue(S_OK == pXORStream->SetFilePointer(0LL, FILE_BEGIN, NULL));
        Assert::IsTrue(S_OK == pXORStream->Read(data, sizeof(data), &ullDataRead));
        Assert::IsTrue(ullDataRead == sizeof(data));
        Assert::IsTrue(!memcmp(data, expectedResult, sizeof(expectedResult)));
    }
};
}  // namespace Orc::Test