//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <SetupAPI.h>

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API EnumDisk
{
public:
    class PhysicalDisk
    {
    public:
        PhysicalDisk() = default;
        PhysicalDisk(PhysicalDisk&& other) noexcept = default;
        std::wstring InterfacePath;
    };

public:
    HRESULT EnumerateDisks(std::vector<PhysicalDisk>& disks, const GUID guidDeviceClass = GUID_DEVINTERFACE_DISK);

    ~EnumDisk();

private:

    HRESULT GetDevice(HDEVINFO hDevInfo, DWORD Index, PhysicalDisk& aDisk, const GUID guidDeviceClass);
};

}  // namespace Orc

#pragma managed(pop)
