//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "LogFileWriter.h"
#include "DiskExtent.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(DiskExtentTest)
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

    TEST_METHOD(DiskExtentBasicTest) {{CDiskExtent diskExtent(_L_);
    Assert::IsTrue(diskExtent.GetName().empty());
    Assert::IsTrue(diskExtent.GetHandle() == INVALID_HANDLE_VALUE);
    Assert::IsTrue(diskExtent.GetLogicalSectorSize() == 0);
    Assert::IsTrue(diskExtent.GetStartOffset() == 0);
    Assert::IsTrue(diskExtent.GetLength() == 0);
}

{
    CDiskExtent diskExtent(_L_, L"Test");
    Assert::IsTrue(!std::wcscmp(diskExtent.GetName().c_str(), L"Test"));
    Assert::IsTrue(diskExtent.GetHandle() == INVALID_HANDLE_VALUE);
    Assert::IsTrue(diskExtent.GetLogicalSectorSize() == 0);
    Assert::IsTrue(diskExtent.GetStartOffset() == 0);
    Assert::IsTrue(diskExtent.GetLength() == 0);
}

{
    CDiskExtent diskExtent(_L_, L"Test", 0x10000, 0x10000, 0x200);
    CheckExpectedDiskExtent(diskExtent);

    {
        CDiskExtent diskExtent2(diskExtent);  // copy constructor
        CheckExpectedDiskExtent(diskExtent2);
    }

    {
        CDiskExtent diskExtent2(_L_);  // operator =
        diskExtent2 = diskExtent;
        CheckExpectedDiskExtent(diskExtent2);
    }

    {
        typedef std::function<CDiskExtent(CDiskExtent)> Functor;

        Functor functor = [](CDiskExtent diskExtent) { return diskExtent; };

        CDiskExtent diskExtent2(_L_);
        diskExtent2 = functor(diskExtent);  // move assignment operator
        CheckExpectedDiskExtent(diskExtent2);
    }

    {
        CDiskExtent diskExtent2 = std::move(diskExtent);  // move constructor
        CheckExpectedDiskExtent(diskExtent2);

        Assert::IsTrue(diskExtent.GetName().empty());
        Assert::IsTrue(diskExtent.GetHandle() == INVALID_HANDLE_VALUE);
    }
}
};  // namespace Orc::Test

void CheckExpectedDiskExtent(const CDiskExtent& diskExtent)
{
    Assert::IsTrue(!std::wcscmp(diskExtent.GetName().c_str(), L"Test"));
    Assert::IsTrue(diskExtent.GetHandle() == INVALID_HANDLE_VALUE);
    Assert::IsTrue(diskExtent.GetLogicalSectorSize() == 0x200);
    Assert::IsTrue(diskExtent.GetStartOffset() == 0x10000);
    Assert::IsTrue(diskExtent.GetLength() == 0x10000);
}
}
;
}