//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "Partition.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(PartitionTest)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize) {}

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(PartitionBasicTest)
    {
        Partition p;
        Assert::IsTrue(p.SysFlags == 0);
        Assert::IsTrue(!p.IsValid());
        Assert::IsTrue(p.PartitionFlags == Partition::Flags::None);
        Assert::IsTrue(p.Start == 0);
        Assert::IsTrue(p.End == 0);
        Assert::IsTrue(p.Size == 0);
        Assert::IsTrue(p.PartitionNumber == 0);
        Assert::IsTrue(p.SectorSize == 0);

        p.PartitionNumber = 1;
        p.SysFlags = 7;
        p.PartitionType = Partition::Type::NTFS;
        p.PartitionFlags = Partition::Flags::Bootable;
        p.Start = 0x10000;
        p.End = 0x20000;
        p.Size = 0x10000;
        p.SectorSize = 0x200;

        {
            Partition p2(p);  // copy constructor
            CheckExpectedPartition(p2);
        }

        {
            Partition p2;
            p2 = p;  // operator=
            CheckExpectedPartition(p2);
        }

        {
            Partition p2 = std::move(p);  // move constructor
            CheckExpectedPartition(p2);
        }

        {
            std::vector<Partition> vm;
            vm.push_back(Partition(p));  // move assignment operator
            CheckExpectedPartition(vm[0]);
        }
    };

    void CheckExpectedPartition(const Partition& p)
    {
        Assert::IsTrue(p.SysFlags == 7);
        Assert::IsTrue(p.IsValid());
        Assert::IsTrue(p.PartitionFlags == Partition::Flags::Bootable);
        Assert::IsTrue(p.Start == 0x10000);
        Assert::IsTrue(p.End == 0x20000);
        Assert::IsTrue(p.Size == 0x10000);
        Assert::IsTrue(p.PartitionNumber == 1);
        Assert::IsTrue(p.SectorSize == 0x200);
    }
};
}  // namespace Orc::Test
