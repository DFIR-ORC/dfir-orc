//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Partition.h"

#include "Text/Print.h"

#include <Partition.h>
#include <PartitionFlags.h>

#include "Text/Fmt/Partition.h"
#include "Text/Fmt/PartitionType.h"
#include "Text/Fmt/PartitionFlags.h"

namespace Orc {
namespace Text {

void Print(Tree& root, const Partition& partition)
{
    if (partition.IsValid())
    {
        auto flags = ToString(partition.PartitionFlags);
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
