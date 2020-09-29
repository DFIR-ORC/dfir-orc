//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "FatFileEntry.h"

#include "BinaryBuffer.h"

#include <memory>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(FatFileEntryTest)
{

private:
    static const DWORD g_TestFileSize = 10240;
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize)
    {
    }

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(FatFileEntryBasicTest)
    {
        // check GetFullName method
        FatFileEntry f1;
        f1.m_LongName = std::wstring(L"parent_folder1");
        f1.m_ParentFolder = nullptr;

        FatFileEntry f2;
        f2.m_LongName = std::wstring(L"parent_folder2");
        f2.m_ParentFolder = &f1;

        FatFileEntry f3;
        f3.m_LongName = std::wstring(L"file.txt");
        f3.m_ParentFolder = &f2;

        std::wstring fullPath = f3.GetFullName();
        std::wstring expectedPath = std::wstring(L"\\parent_folder1\\parent_folder2\\file.txt");
        Assert::IsTrue(!expectedPath.compare(fullPath));

        unsigned char data[112] = {0xF8, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF,
                                   0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x0F,
                                   0x08, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x0B, 0x00,
                                   0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00,
                                   0x0F, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x12, 0x00,
                                   0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00,
                                   0x16, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x19, 0x00,
                                   0x00, 0x00, 0x1A, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x0F, 0x00, 0x00, 0x00, 0x00};

        // check FillSegmentDetailsMap method
        CBinaryBuffer buffer(data, sizeof(data));
        FatTable fatTable(FatTable::FAT32);
        fatTable.AddChunk(std::make_shared<CBinaryBuffer>(buffer));
        fatTable.FillClusterChain(7, f2.m_ClusterChain);
        f2.m_ulSize = g_TestFileSize;
        f2.FillSegmentDetailsMap(0x1000, 0x200);

        // when segment merging is implemented (for contiguous segments) update the number used in this assert
        Assert::IsTrue(20 == f2.m_SegmentDetailsMap.size());

        DWORD size = 0;
        std::for_each(
            std::begin(f2.m_SegmentDetailsMap),
            std::end(f2.m_SegmentDetailsMap),
            [&size](const SegmentDetailsMap::value_type& mapEntry) { size += mapEntry.second.mSize; });

        Assert::IsTrue(g_TestFileSize == size);
    }
};
}  // namespace Orc::Test
