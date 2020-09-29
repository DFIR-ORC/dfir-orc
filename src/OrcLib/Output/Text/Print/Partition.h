//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Output/Text/Print.h"

#include <Partition.h>

namespace Orc {
namespace Text {

template <typename T>
void Print(Orc::Text::Tree<T>& root, const Partition& partition)
{
    if (partition.IsValid())
    {
        auto flags = Partition::ToString(partition.PartitionFlags);
        if (flags.size())
        {
            flags = fmt::format(L", flags: {}", flags);
        }

        root.Add(
            "Partition: #{}, type: {}, offsets: {:#x}-{:#x}, size: {}{}",
            partition.PartitionNumber,
            partition.PartitionType,
            partition.Start,
            partition.End,
            partition.Size,
            flags);
    }
    else
    {
        root.Add("Partition type: {}{}", partition.PartitionType);
    }
}

}  // namespace Text
}  // namespace Orc
