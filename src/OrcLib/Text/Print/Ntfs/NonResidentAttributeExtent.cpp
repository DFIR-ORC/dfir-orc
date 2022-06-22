//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "NonResidentAttributeExtent.h"

#include "Utils/TypeTraits.h"
#include "Text/Fmt/ByteQuantity.h"
#include "Text/Fmt/Offset.h"

namespace Orc {
namespace Text {

void Print(Tree& root, const MFTUtils::NonResidentAttributeExtent& extent)
{
    if (!extent.bZero)
    {
        root.Add(
            L"LowestVCN: {:#018x}, Offset: {}, Size: {}, Allocated: {}",
            extent.LowestVCN,
            Traits::Offset(extent.DiskOffset),
            extent.DataSize,
            Traits::ByteQuantity(extent.DiskAlloc));
    }
    else
    {
        // Segment is SPARSE, only unallocated zeroes
        root.Add(L"Sparse entry, Size: {}", extent.DataSize);
    }
}

}  // namespace Text
}  // namespace Orc
