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

class SystemStorageReader : public CompleteVolumeReader
{
private:
    UINT m_uiPartNum = (UINT)-1;

    ULONGLONG m_ullOffset = (ULONGLONG)-1;
    ULONGLONG m_ullLength = (ULONGLONG)-1;
    UINT m_uiSectorSize = 512;

protected:
    virtual std::shared_ptr<VolumeReader> DuplicateReader();

public:
    SystemStorageReader(logger pLog, const WCHAR* szLocation)
        : CompleteVolumeReader(std::move(pLog), szLocation)
    {
    }

    const WCHAR* ShortVolumeName() { return L"\\"; }

    HRESULT LoadDiskProperties(void);
    HANDLE GetDevice() { return INVALID_HANDLE_VALUE; }

    ~SystemStorageReader();
};
}  // namespace Orc

constexpr auto REGEX_SYSTEMSTORAGE =
    L"(\\\\\\\\(\\.|\\?)\\\\[^,]+)(,offset=([0-9]+))?(,size=([0-9]+))?(,sector=([0-9]+))?(,part=([0-9]+))?";

constexpr auto REGEX_SYSTEMSTORAGE_SPEC = 1;
constexpr auto REGEX_SYSTEMSTORAGE_PARTITION_SPEC = 9;
constexpr auto REGEX_SYSTEMSTORAGE_PARTITION_NUM = 10;

constexpr auto REGEX_SYSTEMSTORAGE_OFFSET = 4;
constexpr auto REGEX_SYSTEMSTORAGE_SIZE = 6;
constexpr auto REGEX_SYSTEMSTORAGE_SECTOR = 8;

#pragma managed(pop)
