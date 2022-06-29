//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include <CppUnitTest.h>

#include "BinaryBuffer.h"
#include "fmt/core.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

#define LogLine()                                                                                                      \
    {                                                                                                                  \
        auto line = fmt::format("{}:{}", __FILE__, __LINE__);                                                          \
        Microsoft::VisualStudio::CppUnitTestFramework::Logger::WriteMessage(line.c_str());                             \
    }

namespace Orc::Test {
TEST_CLASS(BinaryBufferTest)
{
private:
    LARGE_INTEGER m_Offset;
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize) {}

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(BinaryBufferBasicTest)
    {

        LogLine();

        // check buffer allocation
        {
            LogLine();
            CBinaryBuffer buffer;

            LogLine();
            buffer.SetCount(100);

            LogLine();
            Assert::IsTrue(buffer.OwnsBuffer());

            LogLine();
            Assert::IsTrue(buffer.GetCount() == 100);

            LogLine();
            Assert::IsTrue(buffer.CheckCount(150));

            LogLine();
            Assert::IsTrue(buffer.GetCount() == 150);
        }

        // binary buffer with data + md5 + sha1 + operator[]
        {
            LogLine();
            unsigned char data[5] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
            CBinaryBuffer buffer(data, sizeof(data));

            LogLine();
            Assert::IsTrue(!buffer.OwnsBuffer());

            LogLine();
            Assert::IsTrue(buffer.GetCount() == sizeof(data));

            LogLine();
            Assert::IsTrue(buffer.GetData() != nullptr);

            LogLine();
            Assert::IsTrue(buffer[0] == 0xAA);

            LogLine();
            Assert::IsTrue(buffer[1] == 0xBB);

            LogLine();
            Assert::IsTrue(buffer[2] == 0xCC);

            LogLine();
            Assert::IsTrue(buffer[3] == 0xDD);

            LogLine();
            Assert::IsTrue(buffer[4] == 0xEE);

            LogLine();
            unsigned char md5Result[16] = {
                0x37, 0xD3, 0xA1, 0x9F, 0xF4, 0x69, 0x7F, 0x91, 0xFD, 0xC4, 0xC3, 0xA0, 0x69, 0x0A, 0xD8, 0xC5};
            CBinaryBuffer md5;

            LogLine();
            buffer.GetMD5(md5);

            LogLine();
            Assert::IsTrue(md5.GetCount() == sizeof(md5Result));

            LogLine();
            Assert::IsTrue(!memcmp(md5.GetData(), md5Result, sizeof(md5Result)));

            LogLine();
            unsigned char sha1Result[20] = {0xD5, 0xB4, 0x12, 0x31, 0x9A, 0x66, 0xBE, 0x9D, 0xCB, 0x40,
                                            0xEA, 0x0E, 0xE0, 0x9B, 0xB7, 0x62, 0x71, 0xC5, 0xE7, 0x48};
            CBinaryBuffer sha1;

            LogLine();
            buffer.GetSHA1(sha1);

            LogLine();
            Assert::IsTrue(sha1.GetCount() == sizeof(sha1Result));

            LogLine();
            Assert::IsTrue(!memcmp(sha1.GetData(), sha1Result, sizeof(sha1Result)));

            LogLine();
            buffer.ZeroMe();

            LogLine();
            Assert::IsTrue(buffer[0] == 0x00);

            LogLine();
            Assert::IsTrue(buffer[1] == 0x00);

            LogLine();
            Assert::IsTrue(buffer[2] == 0x00);

            LogLine();
            Assert::IsTrue(buffer[3] == 0x00);

            LogLine();
            Assert::IsTrue(buffer[4] == 0x00);

            // try [] with an invalid index
            bool gotException = false;
            try
            {
                LogLine();
                buffer[5];
            }
            catch (...)
            {
                LogLine();
                gotException = true;
            }

            LogLine();
            Assert::IsTrue(gotException);
        }

        // check copy constructor of a binary buffer
        {
            LogLine();
            CBinaryBuffer buffer;

            LogLine();
            buffer.SetCount(100);

            LogLine();
            buffer[0] = 0xFF;

            LogLine();
            CBinaryBuffer sha1;

            LogLine();
            buffer.GetSHA1(sha1);

            LogLine();
            CBinaryBuffer buffer2(buffer);  // copy constructor (buffer owns its memory)

            LogLine();
            Assert::IsTrue(buffer2.OwnsBuffer());

            LogLine();
            Assert::IsTrue(buffer2.GetCount() == 100);

            LogLine();
            Assert::IsTrue(buffer2[0] == 0xFF);

            LogLine();
            CBinaryBuffer sha1ToCheck;

            LogLine();
            buffer2.GetSHA1(sha1ToCheck);

            LogLine();
            Assert::IsTrue(!memcmp(sha1.GetData(), sha1ToCheck.GetData(), sha1.GetCount()));
        }

        {
            LogLine();
            unsigned char data[5] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
            CBinaryBuffer buffer(data, sizeof(data));

            LogLine();
            CBinaryBuffer buffer2(buffer);  // copy constructor (buffer doesn't own its memory)

            LogLine();
            Assert::IsTrue(!buffer2.OwnsBuffer());

            LogLine();
            Assert::IsTrue(buffer2.GetCount() == 5);
        }

        // check operator= of a binary buffer
        {
            LogLine();
            CBinaryBuffer buffer;

            LogLine();
            buffer.SetCount(100);

            LogLine();
            bool ownsMemory = buffer.OwnsBuffer();

            LogLine();
            size_t size = buffer.GetCount();

            LogLine();
            BYTE* pointer = buffer.GetData();

            LogLine();
            CBinaryBuffer buffer2;

            LogLine();
            buffer2 = buffer;  // operator=

            LogLine();
            Assert::IsTrue(buffer2.OwnsBuffer() == ownsMemory);

            LogLine();
            Assert::IsTrue(buffer2.GetCount() == size);

            LogLine();
            Assert::IsTrue(buffer2.GetData() != pointer);

            LogLine();
            Assert::IsTrue(buffer.OwnsBuffer() == true);

            LogLine();
            Assert::IsTrue(buffer.GetCount() == size);

            LogLine();
            Assert::IsTrue(buffer.GetData() != nullptr);
        }

        // check move constructor / move assignment operator
        {
            LogLine();
            CBinaryBuffer buffer;

            LogLine();
            buffer.SetCount(100);

            LogLine();
            bool ownsMemory = buffer.OwnsBuffer();

            LogLine();
            size_t size = buffer.GetCount();

            LogLine();
            BYTE* pointer = buffer.GetData();

            typedef std::function<CBinaryBuffer(CBinaryBuffer)> Functor;

            LogLine();
            Functor functor = [](CBinaryBuffer buffer) { return buffer; };

            LogLine();
            CBinaryBuffer buffer1;

            LogLine();
            buffer1 = functor(buffer);  // copy constructor is called first then move assignment operator

            LogLine();
            Assert::IsTrue(buffer1.OwnsBuffer() == ownsMemory);

            LogLine();
            Assert::IsTrue(buffer1.GetCount() == size);

            LogLine();
            Assert::IsTrue(buffer1.GetData() != pointer);

            LogLine();
            Assert::IsTrue(buffer.OwnsBuffer() == ownsMemory);

            LogLine();
            Assert::IsTrue(buffer.GetCount() == size);

            LogLine();
            Assert::IsTrue(buffer.GetData() == pointer);

            LogLine();
            ownsMemory = buffer1.OwnsBuffer();  // buffer1 should own its memory

            LogLine();
            size = buffer1.GetCount();

            LogLine();
            pointer = buffer1.GetData();

            LogLine();
            CBinaryBuffer buffer2 = std::move(buffer1);  // move constructor

            LogLine();
            Assert::IsTrue(buffer2.OwnsBuffer() == ownsMemory);

            LogLine();
            Assert::IsTrue(buffer2.GetCount() == size);

            LogLine();
            Assert::IsTrue(buffer2.GetData() == pointer);

            LogLine();
            Assert::IsTrue(buffer1.OwnsBuffer() == true);

            LogLine();
            Assert::IsTrue(buffer1.GetCount() == 0);

            LogLine();
            Assert::IsTrue(buffer1.GetData() == nullptr);
        }
    }
};

}  // namespace Orc::Test
