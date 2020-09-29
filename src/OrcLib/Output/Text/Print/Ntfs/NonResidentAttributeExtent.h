//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Output/Text/Print.h"

#include "Utils/TypeTraits.h"
#include "MFTUtils.h"

namespace Orc {
namespace Text {

template <typename T>
void Print(Orc::Text::Tree<T>& root, const Orc::MFTUtils::NonResidentAttributeExtent& extent)
{
    if (!extent.bZero)
    {
        root.Add(
            L"LowestVCN: {:#018x}, Offset: {}, Allocated size: {}, Size: {}",
            extent.LowestVCN,
            Traits::Offset(extent.DiskOffset),
            Traits::ByteQuantity(extent.DiskAlloc),
            Traits::ByteQuantity(extent.DataSize));
    }
    else
    {
        // Segment is SPARSE, only unallocated zeroes
        root.Add(L"Sparse entry, Size: {}", Traits::ByteQuantity(extent.DataSize));
    }
}

}  // namespace Text
}  // namespace Orc
