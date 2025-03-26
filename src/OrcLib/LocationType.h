//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string>

namespace Orc {

enum class LocationType
{
    Undetermined = 0,
    OfflineMFT,
    ImageFileDisk,
    ImageFileVolume,
    DiskInterface,
    DiskInterfaceVolume,
    PhysicalDrive,
    PhysicalDriveVolume,
    SystemStorage,
    SystemStorageVolume,
    PartitionVolume,
    MountedStorageVolume,
    MountedVolume,
    Snapshot
};

std::wstring ToString(LocationType locationType);

}  // namespace Orc
