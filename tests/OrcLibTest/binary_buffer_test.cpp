//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "BinaryBuffer.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(BinaryBufferTest)
{
private:
    LARGE_INTEGER m_Offset;
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize)
    {
    }

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(BinaryBufferBasicTest)
    {
        // check buffer allocation
        {
            CBinaryBuffer buffer;
            buffer.SetCount(100);
            Assert::IsTrue(buffer.OwnsBuffer());
            Assert::IsTrue(buffer.GetCount() == 100);
            Assert::IsTrue(buffer.CheckCount(150));
            Assert::IsTrue(buffer.GetCount() == 150);
        }

        // binary buffer with data + md5 + sha1 + operator[]
        {
            unsigned char data[5] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
            CBinaryBuffer buffer(data, sizeof(data));

            Assert::IsTrue(!buffer.OwnsBuffer());
            Assert::IsTrue(buffer.GetCount() == sizeof(data));

            Assert::IsTrue(buffer.GetData() != nullptr);
            Assert::IsTrue(buffer[0] == 0xAA);
            Assert::IsTrue(buffer[1] == 0xBB);
            Assert::IsTrue(buffer[2] == 0xCC);
            Assert::IsTrue(buffer[3] == 0xDD);
            Assert::IsTrue(buffer[4] == 0xEE);

            unsigned char md5Result[16] = {
                0x37, 0xD3, 0xA1, 0x9F, 0xF4, 0x69, 0x7F, 0x91, 0xFD, 0xC4, 0xC3, 0xA0, 0x69, 0x0A, 0xD8, 0xC5};
            CBinaryBuffer md5;
            buffer.GetMD5(md5);
            Assert::IsTrue(md5.GetCount() == sizeof(md5Result));
            Assert::IsTrue(!memcmp(md5.GetData(), md5Result, sizeof(md5Result)));

            unsigned char sha1Result[20] = {0xD5, 0xB4, 0x12, 0x31, 0x9A, 0x66, 0xBE, 0x9D, 0xCB, 0x40,
                                            0xEA, 0x0E, 0xE0, 0x9B, 0xB7, 0x62, 0x71, 0xC5, 0xE7, 0x48};
            CBinaryBuffer sha1;
            buffer.GetSHA1(sha1);
            Assert::IsTrue(sha1.GetCount() == sizeof(sha1Result));
            Assert::IsTrue(!memcmp(sha1.GetData(), sha1Result, sizeof(sha1Result)));

            buffer.ZeroMe();
            Assert::IsTrue(buffer[0] == 0x00);
            Assert::IsTrue(buffer[1] == 0x00);
            Assert::IsTrue(buffer[2] == 0x00);
            Assert::IsTrue(buffer[3] == 0x00);
            Assert::IsTrue(buffer[4] == 0x00);

            // try [] with an invalid index
            bool gotException = false;
            try
            {
                buffer[5];
            }
            catch (...)
            {
                gotException = true;
            }

            Assert::IsTrue(gotException);
        }

        // check copy constructor of a binary buffer
        {{CBinaryBuffer buffer;
        buffer.SetCount(100);
        buffer[0] = 0xFF;
        CBinaryBuffer sha1;
        buffer.GetSHA1(sha1);

        CBinaryBuffer buffer2(buffer);  // copy constructor (buffer owns its memory)
        Assert::IsTrue(buffer2.OwnsBuffer());
        Assert::IsTrue(buffer2.GetCount() == 100);
        Assert::IsTrue(buffer2[0] == 0xFF);

        CBinaryBuffer sha1ToCheck;
        buffer2.GetSHA1(sha1ToCheck);

        Assert::IsTrue(!memcmp(sha1.GetData(), sha1ToCheck.GetData(), sha1.GetCount()));
    }

    {
        unsigned char data[5] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
        CBinaryBuffer buffer(data, sizeof(data));

        CBinaryBuffer buffer2(buffer);  // copy constructor (buffer doesn't own its memory)
        Assert::IsTrue(!buffer2.OwnsBuffer());
        Assert::IsTrue(buffer2.GetCount() == 5);
    }
}

// check operator= of a binary buffer
{
    CBinaryBuffer buffer;
    buffer.SetCount(100);

    bool ownsMemory = buffer.OwnsBuffer();
    size_t size = buffer.GetCount();
    BYTE* pointer = buffer.GetData();

    CBinaryBuffer buffer2;
    buffer2 = buffer;  // operator=

    Assert::IsTrue(buffer2.OwnsBuffer() == ownsMemory);
    Assert::IsTrue(buffer2.GetCount() == size);
    Assert::IsTrue(buffer2.GetData() != pointer);

    Assert::IsTrue(buffer.OwnsBuffer() == true);
    Assert::IsTrue(buffer.GetCount() == size);
    Assert::IsTrue(buffer.GetData() != nullptr);
}

// check move constructor / move assignment operator
{
    CBinaryBuffer buffer;
    buffer.SetCount(100);

    bool ownsMemory = buffer.OwnsBuffer();
    size_t size = buffer.GetCount();
    BYTE* pointer = buffer.GetData();

    typedef std::function<CBinaryBuffer(CBinaryBuffer)> Functor;

    Functor functor = [](CBinaryBuffer buffer) { return buffer; };

    CBinaryBuffer buffer1;
    buffer1 = functor(buffer);  // copy constructor is called first then move assignment operator
    Assert::IsTrue(buffer1.OwnsBuffer() == ownsMemory);
    Assert::IsTrue(buffer1.GetCount() == size);
    Assert::IsTrue(buffer1.GetData() != pointer);

    Assert::IsTrue(buffer.OwnsBuffer() == ownsMemory);
    Assert::IsTrue(buffer.GetCount() == size);
    Assert::IsTrue(buffer.GetData() == pointer);

    ownsMemory = buffer1.OwnsBuffer();  // buffer1 should own its memory
    size = buffer1.GetCount();
    pointer = buffer1.GetData();

    CBinaryBuffer buffer2 = std::move(buffer1);  // move constructor
    Assert::IsTrue(buffer2.OwnsBuffer() == ownsMemory);
    Assert::IsTrue(buffer2.GetCount() == size);
    Assert::IsTrue(buffer2.GetData() == pointer);

    Assert::IsTrue(buffer1.OwnsBuffer() == true);
    Assert::IsTrue(buffer1.GetCount() == 0);
    Assert::IsTrue(buffer1.GetData() == nullptr);
}
}  // namespace Orc::Test
}
;
}
