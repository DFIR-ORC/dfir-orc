//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "FatTable.h"

#include "BinaryBuffer.h"

#include <memory>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(FatTableTest)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize)
    {
    }

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(Fat32TableBasicTest)
    {
        // fat32 table data
        FatTable fatTable(FatTable::FAT32);

        unsigned char data1[64] = {0xF8, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0xFF,
                                   0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF,
                                   0xFF, 0x0F, 0x08, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00,
                                   0x00, 0x0B, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x00,
                                   0x0E, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00};

        CBinaryBuffer buffer1(data1, sizeof(data1));
        fatTable.AddChunk(std::make_shared<CBinaryBuffer>(buffer1));

        unsigned char data2[48] = {0x11, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00,
                                   0x14, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00,
                                   0x17, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00,
                                   0x1A, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x0F, 0x00, 0x00, 0x00, 0x00};

        CBinaryBuffer buffer2(data2, sizeof(data2));
        fatTable.AddChunk(std::make_shared<CBinaryBuffer>(buffer2));

        Assert::IsTrue(2 == fatTable.GetFatTableChunks().size());

        {
            // root folder
            FatTableEntry entry;
            Assert::IsTrue(S_OK == fatTable.GetEntry(2, entry));
            Assert::IsTrue(true == entry.IsEOFEntry());

            FatTable::ClusterChain clusterChain;
            fatTable.FillClusterChain(2, clusterChain);
            Assert::IsTrue(1 == clusterChain.size());
        }

        {
            // file
            FatTableEntry entry;
            Assert::IsTrue(S_OK == fatTable.GetEntry(7, entry));
            Assert::IsTrue(true == entry.IsUsed());

            FatTable::ClusterChain clusterChain;
            fatTable.FillClusterChain(7, clusterChain);
            Assert::IsTrue(20 == clusterChain.size());
        }

        {
            // file
            FatTableEntry entry;
            Assert::IsTrue(S_OK == fatTable.GetEntry(0x1B, entry));
            Assert::IsTrue(true == entry.IsFree());
        }
    }
};
}  // namespace Orc::Test
