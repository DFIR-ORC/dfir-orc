//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl
//

#include "stdafx.h"

#include "PartitionFlags.h"

#include <boost/algorithm/string/join.hpp>

namespace Orc {

std::wstring ToString(PartitionFlags flags)
{
    std::vector<std::wstring> activeFlags;

    if ((flags & PartitionFlags::Bootable) == PartitionFlags::Bootable)
    {
        activeFlags.push_back(L"BOOTABLE");
    }

    if ((flags & PartitionFlags::Hidden) == PartitionFlags::Hidden)
    {
        activeFlags.push_back(L"HIDDEN");
    }

    if ((flags & PartitionFlags::Invalid) == PartitionFlags::Invalid)
    {
        activeFlags.push_back(L"INVALID");
    }

    if ((flags & PartitionFlags::NoAutoMount) == PartitionFlags::NoAutoMount)
    {
        activeFlags.push_back(L"NO_AUTO_MOUNT");
    }

    if ((flags & PartitionFlags::None) == PartitionFlags::None)
    {
        activeFlags.push_back(L"NONE");
    }

    if ((flags & PartitionFlags::ReadOnly) == PartitionFlags::ReadOnly)
    {
        activeFlags.push_back(L"READONLY");
    }

    if ((flags & PartitionFlags::System) == PartitionFlags::System)
    {
        activeFlags.push_back(L"SYSTEM");
    }

    return boost::join(activeFlags, L" ");
}

}  // namespace Orc
