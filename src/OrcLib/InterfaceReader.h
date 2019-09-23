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

constexpr auto REGEX_INTERFACE =
    L"(\\\\\\\\(\\?|\\.)\\\\(scsi|usbstor|ide)#[^,]+)(,offset=([0-9]+))?(,size=([0-9]+))?(,sector=([0-9]+))?(,part=([0-"
    L"9]+))?";
constexpr auto REGEX_INTERFACE_SPEC = 1;
constexpr auto REGEX_INTERFACE_PARTITION_SPEC = 10;
constexpr auto REGEX_INTERFACE_PARTITION_NUM = 11;

constexpr auto REGEX_INTERFACE_OFFSET = 5;
constexpr auto REGEX_INTERFACE_SIZE = 7;
constexpr auto REGEX_INTERFACE_SECTOR = 9;

namespace Orc {

class InterfaceReader : public CompleteVolumeReader
{
private:
    UINT m_uiPartNum = (UINT)-1;

    ULONGLONG m_ullOffset = (ULONGLONG)-1;
    ULONGLONG m_ullLength = (ULONGLONG)-1;
    UINT m_uiSectorSize = 512;

    std::wstring m_InterfacePath;

protected:
    virtual std::shared_ptr<VolumeReader> DuplicateReader();

public:
    InterfaceReader(logger pLog, const WCHAR* szLocation)
        : CompleteVolumeReader(std::move(pLog), szLocation) {};
    InterfaceReader(InterfaceReader&&) noexcept = default;

    const WCHAR* ShortVolumeName() { return L"\\"; }

    HRESULT LoadDiskProperties(void);
    HANDLE GetDevice() { return INVALID_HANDLE_VALUE; }
    ULONGLONG GetOffset() { return m_ullOffset; }
    ULONGLONG GetLength() { return m_ullLength; }
    std::wstring GetPath() { return m_InterfacePath; }

    ~InterfaceReader();
};

}  // namespace Orc
#pragma managed(pop)
