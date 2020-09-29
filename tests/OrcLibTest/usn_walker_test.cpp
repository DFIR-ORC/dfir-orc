//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "USNJournalWalker.h"
#include "USNRecordFileInfo.h"
#include "LocationSet.h"
#include "VolumeReader.h"
#include "FileInfo.h"

#include <memory>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(USNWalkerTest)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize) {}

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(USNWalkerBasicTest)
    {
        m_NbFiles = 0;
        m_NbFolders = 0;

        LocationSet locSet;
        locSet.SetPopulatePhysicalDrives(false);
        locSet.SetPopulateShadows(false);
        locSet.SetPopulateSystemObjects(false);
        Assert::IsTrue(S_OK == locSet.EnumerateLocations());
        Assert::IsTrue(S_OK == locSet.Consolidate(false, FSVBR::FSType::NTFS));

        const auto& locs = locSet.GetUniqueLocations();
        std::shared_ptr<Location> loc;

        for_each(std::begin(locs), std::end(locs), [&loc](const std::shared_ptr<Location>& location) {
            if (!loc && location && location->GetType() == Location::Type::MountedVolume)
            {
                loc = location;
            }
        });

        if (loc != nullptr)
        {

            USNJournalWalker walker;
            IUSNJournalWalker::Callbacks callbacks;

            callbacks.RecordCallback =
                [this](std::shared_ptr<VolumeReader>& volreader, WCHAR* szFullName, USN_RECORD* pElt) {
                    std::vector<Filter> filters;
                    Authenticode authenticode;

                    USNRecordFileInfo fi(L"Test", volreader, Intentions::FILEINFO_FILESIZE, filters, szFullName, pElt, authenticode);

                    if (fi.IsDirectory())
                    {
                        m_NbFolders++;
                    }
                    else
                    {
                        m_NbFiles++;
                    }
                };

            Assert::IsTrue(S_OK == walker.Initialize(loc));
            Assert::IsTrue(S_OK == walker.EnumJournal(callbacks));

            Assert::IsTrue(m_NbFiles > 0);
            Assert::IsTrue(m_NbFolders > 0);
        }
        else
        {
            spdlog::error("Failed to find a suitable location to parse (not running as admin?)");
        }
    }

private:
    DWORD64 m_NbFiles;
    DWORD64 m_NbFolders;
};
}  // namespace Orc::Test
