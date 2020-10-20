//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "VolumeShadowCopies.h"

#include "DiskExtent.h"
#include "FSVBR.h"

#include <string>
#include <vector>
#include <memory>

#pragma managed(push, off)

namespace Orc {

class VolumeReader;
class LocationSet;

class ConfigItem;

class ORCLIB_API Location
{
    friend class LocationSet;

public:
    enum class Type
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
        Snapshot,
    };

    static std::wstring ToString(Type locationType);

private:
    std::shared_ptr<VolumeReader> m_Reader;
    std::wstring m_Location;
    std::wstring m_Identifier;
    std::vector<CDiskExtent> m_Extents;
    Type m_Type = Type::Undetermined;
    std::vector<std::wstring> m_Paths;
    std::vector<std::wstring> m_SubDirs;

    std::shared_ptr<VolumeShadowCopies::Shadow> m_Shadow;

    bool m_bParse = false;
    bool m_bIsValid = false;

    void MakeIdentifier();

    void SetShadow(const VolumeShadowCopies::Shadow& shadow)
    {
        m_Shadow = std::make_shared<VolumeShadowCopies::Shadow>(shadow);
    }

public:
    Location(const std::wstring& Location, Type type);

    const std::wstring& GetLocation() const { return m_Location; }
    const std::wstring& GetIdentifier()
    {
        if (m_Identifier.empty())
            MakeIdentifier();
        return m_Identifier;
    }
    const std::vector<std::wstring>& GetSubDirs() const { return m_SubDirs; }
    const std::vector<std::wstring>& GetPaths() const { return m_Paths; }
    Location::Type GetType() const { return m_Type; }
    bool GetParse() const { return m_bParse; }
    bool IsValid() const { return m_bIsValid; }
    ULONGLONG SerialNumber() const;
    const std::shared_ptr<VolumeShadowCopies::Shadow>& GetShadow() const { return m_Shadow; }

    void SetParse(bool toParse) { m_bParse = toParse; }
    void SetIsValid(bool bIsValid) { m_bIsValid = bIsValid; }

    // fs type accessor
    FSVBR::FSType GetFSType() const;
    bool IsNTFS() const { return GetFSType() == FSVBR::FSType::NTFS || GetFSType() == FSVBR::FSType::BITLOCKER; }
    bool IsFAT12() const { return GetFSType() == FSVBR::FSType::FAT12; }
    bool IsFAT16() const { return GetFSType() == FSVBR::FSType::FAT16; }
    bool IsFAT32() const { return GetFSType() == FSVBR::FSType::FAT32; }

    // reader object
    std::shared_ptr<VolumeReader> GetReader();
};

std::wostream& operator<<(std::wostream& o, const Location& l);
}  // namespace Orc

#pragma managed(pop)
