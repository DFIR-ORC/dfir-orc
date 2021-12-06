//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "CompleteVolumeReader.h"

#pragma managed(push, off)

namespace Orc {

class PhysicalDiskReader : public CompleteVolumeReader
{
private:
    UINT m_uiDiskNum = (UINT)-1;
    UINT m_uiPartNum = (UINT)-1;

    ULONGLONG m_ullOffset = (ULONGLONG)-1;
    ULONGLONG m_ullLength = (ULONGLONG)-1;
    UINT m_uiSectorSize = 512;

    WCHAR m_szPhysDrive[MAX_PATH];
    WCHAR m_szDiskGUID[MAX_PATH];

protected:
    virtual std::shared_ptr<VolumeReader> DuplicateReader();

public:
    PhysicalDiskReader(const WCHAR* szLocation);

    void Accept(VolumeReaderVisitor& visitor) const override { return visitor.Visit(*this); }

    const WCHAR* ShortVolumeName() { return L"\\"; }

    HRESULT LoadDiskProperties(void);
    HANDLE GetDevice() { return INVALID_HANDLE_VALUE; }
    ULONGLONG GetOffset() { return m_ullOffset; }
    ULONGLONG GetLength() { return m_ullLength; }
    WCHAR* GetPath() { return m_szPhysDrive; }

    ~PhysicalDiskReader(void);
};

}  // namespace Orc

constexpr auto REGEX_PHYSICALDRIVE =
    L"\\\\\\\\(\\.|\\?)\\\\PhysicalDrive([0-9]+)(,offset=([0-9]+))?(,size=([0-9]+))?(,sector=([0-9]+))?(,part=([0-9]+))"
    L"?";
constexpr auto REGEX_PHYSICALDRIVE_NUM = 2;
constexpr auto REGEX_PHYSICALDRIVE_PARTITION_SPEC = 9;
constexpr auto REGEX_PHYSICALDRIVE_PARTITION_NUM = 10;

constexpr auto REGEX_PHYSICALDRIVE_OFFSET = 4;
constexpr auto REGEX_PHYSICALDRIVE_SIZE = 6;
constexpr auto REGEX_PHYSICALDRIVE_SECTOR = 8;

constexpr auto REGEX_DISK =
    L"\\\\\\\\(\\.|\\?)\\\\Disk(\\{[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}\\})(,offset=([0-9]+))?("
    L",size=([0-9]+))?(,sector=([0-9]+))?(,part=([0-9]+))?";
constexpr auto REGEX_DISK_GUID = 2;
constexpr auto REGEX_DISK_PARTITION_SPEC = 3;
constexpr auto REGEX_DISK_PARTITION_NUM = 9;

constexpr auto REGEX_DISK_OFFSET = 4;
constexpr auto REGEX_DISK_SIZE = 6;
constexpr auto REGEX_DISK_SECTOR = 8;

#pragma managed(pop)
