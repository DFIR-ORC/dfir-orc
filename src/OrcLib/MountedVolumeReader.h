//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "CompleteVolumeReader.h"

#pragma managed(push, off)

constexpr auto MAX_VOLUME_NAME = MAX_PATH;

namespace Orc {
// CNtfs
class ORCLIB_API MountedVolumeReader : public CompleteVolumeReader
{
public:
    MountedVolumeReader(MountedVolumeReader&&) noexcept = default;
    MountedVolumeReader(logger pLog, const WCHAR* szLocation);

    void Accept(VolumeReaderVisitor& visitor) const override { return visitor.Visit(*this); }

    const WCHAR* ShortVolumeName() { return m_szShortVolumeName; }
    const WCHAR* VolumeName() { return m_szVolumeName; }
    HANDLE GetDevice() { return m_hDevice; }

    HRESULT LoadDiskProperties(void);

    HRESULT Close()
    {
        if (m_hDevice != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_hDevice);
            m_hDevice = INVALID_HANDLE_VALUE;
        }
        return S_OK;
    }

    ~MountedVolumeReader() { Close(); }

protected:
    virtual std::shared_ptr<VolumeReader> DuplicateReader();

private:
    HANDLE m_hDevice = INVALID_HANDLE_VALUE;

    WCHAR m_Name;
    WCHAR m_szVolumeName[MAX_VOLUME_NAME];
    WCHAR m_szShortVolumeName[MAX_VOLUME_NAME];
    WCHAR m_VolumePath[MAX_PATH];

    HRESULT GetDiskExtents();
};
}  // namespace Orc

// \\\\?\\Volume{e1404546-e2f7-11e0-96f1-806e6f6e6963}\\ 
constexpr auto  REGEX_MOUNTED_VOLUME = L"(\\\\\\\\(\\?|\\.)\\\\Volume(\\{[a-zA-Z0-9]{8}-[a-zA-Z0-9]{4}-[a-zA-Z0-9]{4}-[a-zA-Z0-9]{4}-[a-zA-Z0-9]{12}\\})).*";
constexpr auto REGEX_MOUNTED_VOLUME_ROOT = 2;
constexpr auto REGEX_MOUNTED_VOLUME_ID = 3;

// \\?\GLOBALROOT\Device\HarddiskVolume3 || \\.\HarddiskVolume1 https://regex101.com/r/2Trar8/3
constexpr auto REGEX_MOUNTED_HARDDISKVOLUME =
    LR"RAW((\\\\((\?|\.)(\\GLOBALROOT)?)?)?(\\Device)?\\HarddiskVolume([0-9]+).*)RAW";
constexpr auto REGEX_MOUNTED_HARDDISKVOLUME_ID = 6;

// \\\\?\\C:\\[\\subdir]
constexpr auto REGEX_MOUNTED_DRIVE = L"(\\\\\\\\(\\?|\\.)\\\\)?([a-zA-Z]):((\\\\)?.*)$";
constexpr auto REGEX_MOUNTED_DRIVE_LETTER = 3;
constexpr auto REGEX_MOUNTED_DRIVE_SUBDIR = 4;

constexpr auto REGEX_PARTITIONVOLUME = L"\\\\\\\\(\\.|\\?)\\\\Harddisk([0-9]+)Partition([0-9]+)";
constexpr auto REGEX_PARTITIONVOLUME_DISK_SPEC = 2;
constexpr auto REGEX_PARTITIONVOLUME_PARTITION_SPEC = 3;

#pragma managed(pop)
