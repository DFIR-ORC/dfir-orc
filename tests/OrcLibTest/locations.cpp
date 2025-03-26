//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "LocationSet.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(LocationsTest)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize) {}

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(LocationsSetBasicTest)
    {
        LocationSet::Locations locations;

        {
            std::shared_ptr<Location> loc0 = std::make_shared<Location>(L"Test0", Location::Type::PhysicalDriveVolume);
            locations.insert(std::make_pair(L"Test0", loc0));

            std::shared_ptr<Location> loc1 = std::make_shared<Location>(L"Test1", Location::Type::ImageFileDisk);
            locations.insert(std::make_pair(L"Test1", loc1));

            std::shared_ptr<Location> loc2 = std::make_shared<Location>(L"Test2", Location::Type::ImageFileVolume);
            locations.insert(std::make_pair(L"Test2", loc2));

            std::shared_ptr<Location> loc3 = std::make_shared<Location>(L"Test3", Location::Type::MountedVolume);
            locations.insert(std::make_pair(L"Test3", loc3));

            Assert::IsTrue(4 == locations.size());
            LocationSet::EliminateUselessLocations(locations);
            Assert::IsTrue(2 == locations.size());
            Assert::IsTrue(locations.find(L"Test1") != locations.end());
            Assert::IsTrue(locations.find(L"Test2") != locations.end());
        }

        {
            locations.clear();
            std::shared_ptr<Location> loc1 = std::make_shared<Location>(L"Test1", Location::Type::ImageFileDisk);
            locations.insert(std::make_pair(L"Test1", loc1));

            Assert::IsTrue(1 == locations.size());
            LocationSet::EliminateUselessLocations(locations);
            Assert::IsTrue(1 == locations.size());
        }

        {
            locations.clear();
            std::shared_ptr<Location> loc2 = std::make_shared<Location>(L"Test2", Location::Type::ImageFileVolume);
            locations.insert(std::make_pair(L"Test2", loc2));

            Assert::IsTrue(1 == locations.size());
            LocationSet::EliminateUselessLocations(locations);
            Assert::IsTrue(1 == locations.size());
        }

        {
            locations.clear();
            std::shared_ptr<Location> loc0 = std::make_shared<Location>(L"Test0", Location::Type::PhysicalDriveVolume);
            locations.insert(std::make_pair(L"Test0", loc0));

            Assert::IsTrue(1 == locations.size());
            LocationSet::EliminateUselessLocations(locations);  // no image disk
            Assert::IsTrue(1 == locations.size());
        }

        {
            locations.clear();
            std::shared_ptr<Location> loc0 = std::make_shared<Location>(L"Test0", Location::Type::PhysicalDriveVolume);
            locations.insert(std::make_pair(L"Test0", loc0));

            std::shared_ptr<Location> loc1 = std::make_shared<Location>(L"Test1", Location::Type::ImageFileDisk);
            loc1->SetIsValid(true);
            locations.insert(std::make_pair(L"Test1", loc1));

            Assert::IsTrue(2 == locations.size());
            LocationSet::EliminateInvalidLocations(locations);
            Assert::IsTrue(1 == locations.size());
            Assert::IsTrue(locations.find(L"Test1") != locations.end());
        }
    }

    TEST_METHOD(OnlineLocations)
    {
        HRESULT hr = E_FAIL;

        LocationSet aSet;
        aSet.SetPopulatePhysicalDrives(false);

        Assert::IsTrue(S_OK == aSet.EnumerateLocations());
        Assert::IsTrue(S_OK == aSet.Consolidate(true, FSVBR::FSType::ALL));
        Assert::IsTrue(S_OK == aSet.PrintLocationsByVolume(false));
    }

private:
};
}  // namespace Orc::Test
