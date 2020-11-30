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

    if (HasFlag(flags, PartitionFlags::Bootable))
    {
        activeFlags.push_back(L"BOOTABLE");
    }

    if (HasFlag(flags, PartitionFlags::Hidden))
    {
        activeFlags.push_back(L"HIDDEN");
    }

    if (HasFlag(flags, PartitionFlags::Invalid))
    {
        activeFlags.push_back(L"INVALID");
    }

    if (HasFlag(flags, PartitionFlags::NoAutoMount))
    {
        activeFlags.push_back(L"NO_AUTO_MOUNT");
    }

    if (HasFlag(flags, PartitionFlags::None))
    {
        activeFlags.push_back(L"NONE");
    }

    if (HasFlag(flags, PartitionFlags::ReadOnly))
    {
        activeFlags.push_back(L"READONLY");
    }

    if (HasFlag(flags, PartitionFlags::System))
    {
        activeFlags.push_back(L"SYSTEM");
    }

    return boost::join(activeFlags, L" ");
}

}  // namespace Orc
