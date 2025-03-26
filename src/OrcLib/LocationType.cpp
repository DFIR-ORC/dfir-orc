//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "stdafx.h"

#include "LocationType.h"

namespace Orc {

std::wstring ToString(LocationType locationType)
{
    switch (locationType)
    {
        case LocationType::Undetermined:
            return L"Unknown";
        case LocationType::OfflineMFT:
            return L"OfflineMFT";
        case LocationType::ImageFileDisk:
            return L"ImageFileDisk";
        case LocationType::ImageFileVolume:
            return L"ImageFileVolume";
        case LocationType::DiskInterface:
            return L"DiskInterface";
        case LocationType::DiskInterfaceVolume:
            return L"DiskInterfaceVolume";
        case LocationType::PhysicalDrive:
            return L"PhysicalDrive";
        case LocationType::PhysicalDriveVolume:
            return L"PhysicalDriveVolume";
        case LocationType::SystemStorage:
            return L"SystemStorage";
        case LocationType::SystemStorageVolume:
            return L"SystemStorageVolume";
        case LocationType::PartitionVolume:
            return L"PartitionVolume";
        case LocationType::MountedStorageVolume:
            return L"MountedStorageVolume";
        case LocationType::MountedVolume:
            return L"MountedVolume";
        case LocationType::Snapshot:
            return L"Snapshot";
    }

    return L"Unknown";
}

}  // namespace Orc
