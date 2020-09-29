//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "FatFileEntry.h"
#include "FatStream.h"
#include "CryptoHashStream.h"
#include "DevNullStream.h"

#include "BinaryBuffer.h"
#include "VolumeReaderTest.h"

#include <memory>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(FatStreamTest)
{
private:
    LONGLONG m_Offset;
    static const DWORD g_TestFileSize = 10240;
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize) {}

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(FatStreamBasicTest)
    {
        // fat32 table data
        unsigned char data[112] = {0xF8, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF,
                                   0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x0F,
                                   0x08, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x0B, 0x00,
                                   0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00,
                                   0x0F, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x12, 0x00,
                                   0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00,
                                   0x16, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x19, 0x00,
                                   0x00, 0x00, 0x1A, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x0F, 0x00, 0x00, 0x00, 0x00};

        // create file entry
        FatFileEntry f;
        CBinaryBuffer fatTableBuffer(data, sizeof(data));
        FatTable fatTable(FatTable::FAT32);
        fatTable.AddChunk(std::make_shared<CBinaryBuffer>(fatTableBuffer));
        fatTable.FillClusterChain(7, f.m_ClusterChain);
        f.m_ulSize = g_TestFileSize;
        f.FillSegmentDetailsMap(0x1000, 0x200);

        // create fake volume reader with callbacks
        VolumeReaderTest::SeekCallBack seekCallBack = [this](ULONGLONG offset) {
            m_Offset = offset;

            Log::Info(L"Seek with offset=%llu", offset);
            return S_OK;
        };

        VolumeReaderTest::ReadCallBack readCallBack =
            [this](ULONGLONG offset, CBinaryBuffer& data, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead) {
                Log::Info(L"Read with size=%d", ullBytesToRead);

                ULONGLONG i = 0;
                for (; i < ullBytesToRead; i++)
                {
                    *(data.GetData() + i) = 'A';
                }

                ullBytesRead = ullBytesToRead;
                return S_OK;
            };

        VolumeReaderTest volReader(&seekCallBack, &readCallBack);
        std::shared_ptr<VolumeReaderTest> volReaderPtr = std::make_shared<VolumeReaderTest>(volReader);
        std::shared_ptr<FatFileEntry> fPtr = std::make_shared<FatFileEntry>(f);

        // create FatStream
        FatStream stream(volReaderPtr, fPtr);
        Assert::IsTrue(g_TestFileSize == stream.GetSize());
        Assert::IsTrue(S_OK == stream.CanRead());
        Assert::IsTrue(!(stream.CanWrite() == S_OK));

        ULONG64 ullCurrentPointer = 0;

        // file begin
        Assert::IsTrue(S_OK == stream.SetFilePointer(0, FILE_BEGIN, &ullCurrentPointer));
        Assert::IsTrue(ullCurrentPointer == 0);

        CBinaryBuffer buffer;
        buffer.SetCount(5);
        buffer.ZeroMe();
        ULONG64 ullBytesRead = 0;
        Assert::IsTrue(S_OK == stream.Read(buffer.GetData(), 5, &ullBytesRead));
        Assert::IsTrue(ullBytesRead == 5);
        Assert::IsTrue(*buffer.GetData() == 'A' && *(buffer.GetData() + 2) == 'A' && *(buffer.GetData() + 4) == 'A');

        // file current
        Assert::IsTrue(S_OK == stream.SetFilePointer(5, FILE_CURRENT, &ullCurrentPointer));
        Assert::IsTrue(ullCurrentPointer == 10);

        buffer.ZeroMe();
        Assert::IsTrue(S_OK == stream.Read(buffer.GetData(), 5, &ullBytesRead));
        Assert::IsTrue(ullBytesRead == 5);
        Assert::IsTrue(*buffer.GetData() == 'A' && *(buffer.GetData() + 2) == 'A' && *(buffer.GetData() + 4) == 'A');

        // file end
        Assert::IsTrue(S_OK == stream.SetFilePointer(-5, FILE_END, &ullCurrentPointer));
        Assert::IsTrue(ullCurrentPointer == g_TestFileSize - 5);

        buffer.ZeroMe();
        Assert::IsTrue(S_OK == stream.Read(buffer.GetData(), 5, &ullBytesRead));
        Assert::IsTrue(ullBytesRead == 5);
        Assert::IsTrue(*buffer.GetData() == 'A' && *(buffer.GetData() + 2) == 'A' && *(buffer.GetData() + 4) == 'A');

        // error cases
        Assert::IsTrue(S_OK != stream.SetFilePointer(1, FILE_END, &ullCurrentPointer));
        Assert::IsTrue(S_OK != stream.SetFilePointer(-1, FILE_BEGIN, &ullCurrentPointer));

        // other cases
        Assert::IsTrue(S_OK == stream.SetFilePointer(stream.GetSize() + 1, FILE_BEGIN, &ullCurrentPointer));
        Assert::IsTrue(ullCurrentPointer == stream.GetSize());

        Assert::IsTrue(S_OK == stream.SetFilePointer(0, FILE_BEGIN, &ullCurrentPointer));
        Assert::IsTrue(ullCurrentPointer == 0);

        Assert::IsTrue(S_OK == stream.SetFilePointer(stream.GetSize() + 1, FILE_CURRENT, &ullCurrentPointer));
        Assert::IsTrue(ullCurrentPointer == stream.GetSize());

        Assert::IsTrue(S_OK == stream.SetFilePointer(-1 * (stream.GetSize() + 1), FILE_CURRENT, &ullCurrentPointer));
        Assert::IsTrue(ullCurrentPointer == stream.GetSize());

        Assert::IsTrue(S_OK == stream.SetFilePointer(-1 * (stream.GetSize() + 1), FILE_END, &ullCurrentPointer));
        Assert::IsTrue(ullCurrentPointer == stream.GetSize());

        // check hash values
        CBinaryBuffer sha256;
        CBinaryBuffer sha1;
        CBinaryBuffer md5;

        auto hashstream = std::make_shared<CryptoHashStream>();

        Assert::IsTrue(
            S_OK
            == hashstream->OpenToRead(
                CryptoHashStream::Algorithm::SHA256 | CryptoHashStream::Algorithm::MD5 | CryptoHashStream::Algorithm::SHA1,
                std::make_shared<FatStream>(stream)));

        auto devnull = std::make_shared<DevNullStream>();
        ULONGLONG ullWritten = 0LL;
        Assert::IsTrue(S_OK == hashstream->CopyTo(devnull, &ullWritten));
        Assert::IsTrue(S_OK == hashstream->GetMD5(md5));
        Assert::IsTrue(S_OK == hashstream->GetSHA1(sha1));
        Assert::IsTrue(S_OK == hashstream->GetSHA256(sha256));

        unsigned char md5Result[16] = {
            0xB2, 0xA3, 0xAF, 0xFD, 0xFA, 0x98, 0x05, 0xC9, 0x17, 0xDF, 0x79, 0x10, 0x87, 0xAC, 0x93, 0xD1};
        Assert::IsTrue(md5.GetCount() == sizeof(md5Result));
        Assert::IsTrue(!memcmp(md5.GetData(), md5Result, sizeof(md5Result)));

        unsigned char sha1Result[20] = {0x1C, 0x70, 0xA5, 0xD4, 0x5F, 0xE4, 0x1D, 0xD1, 0x0A, 0x55,
                                        0x37, 0x80, 0xDA, 0x6D, 0x75, 0x2E, 0xA0, 0x45, 0x10, 0x55};
        Assert::IsTrue(sha1.GetCount() == sizeof(sha1Result));
        Assert::IsTrue(!memcmp(sha1.GetData(), sha1Result, sizeof(sha1Result)));

        unsigned char sha256Result[32] = {0xB8, 0xC3, 0x26, 0x92, 0xF7, 0x5B, 0x51, 0xC4, 0x21, 0x69, 0xAC,
                                          0x14, 0x5C, 0x04, 0xD0, 0x45, 0xC8, 0x06, 0xC9, 0xD8, 0x13, 0xC0,
                                          0xFB, 0xC4, 0x5C, 0x1F, 0x0F, 0xD9, 0xF7, 0x3E, 0x4D, 0xA9};
        Assert::IsTrue(sha256.GetCount() == sizeof(sha256Result));
        Assert::IsTrue(!memcmp(sha256.GetData(), sha256Result, sizeof(sha256Result)));
    }
};
}  // namespace Orc::Test
