//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "BufferStream.h"

#include <numeric>
#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(BufferStreamTest)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize) {}

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(Basic)
    {
        auto buf_stream = std::make_shared<BufferStream<MAX_PATH>>();

        std::vector<BYTE> bytes(1024);

        std::generate(std::begin(bytes), std::end(bytes), []() {
            static BYTE i = 0;
            return (i++ % std::numeric_limits<BYTE>::max());
        });

        Assert::IsTrue(S_OK == buf_stream->Open());
        ULONGLONG bytesWritten = 0LLU;
        Assert::IsTrue(S_OK == buf_stream->Write(bytes.data(), bytes.size() * sizeof(BYTE), &bytesWritten));
        Assert::AreEqual(bytesWritten, (ULONGLONG)bytes.size() * sizeof(BYTE));

        for (int i = 1; i < 20; ++i)
        {
            ULONGLONG bytesWritten = 0LLU;
            auto toWrite = (bytes.size() * sizeof(BYTE)) % i;
            Assert::IsTrue(S_OK == buf_stream->Write(bytes.data(), toWrite, &bytesWritten));
            Assert::AreEqual(bytesWritten, (ULONGLONG)toWrite);
        }

        ULONGLONG newPos = 0xFEFEFEFEFEFEFEFE;
        Assert::IsTrue(S_OK == buf_stream->SetFilePointer(0LL, FILE_BEGIN, &newPos));

        ULONGLONG bytesRead = 0LLU;

        BYTE readBuffer[1024];
        Assert::IsTrue(S_OK == buf_stream->Read(readBuffer, 1024 * sizeof(BYTE), &bytesRead));
        Assert::AreEqual(bytesRead, (ULONGLONG)1024 * sizeof(BYTE));
        Assert::IsFalse(std::lexicographical_compare(
            std::begin(bytes), std::end(bytes), std::begin(readBuffer), std::end(readBuffer)));

        do
        {
            Assert::IsTrue(S_OK == buf_stream->Read(readBuffer, 1024 * sizeof(BYTE), &bytesRead));
        } while (bytesRead > 0);
    }
};
}  // namespace Orc::Test
