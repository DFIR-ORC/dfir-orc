//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/Print.h"

#include "Utils/TypeTraits.h"
#include "MFTUtils.h"

namespace Orc {
namespace Text {

template <>
struct Printer<Orc::MFTUtils::NonResidentAttributeExtent>
{
    static void Output(Orc::Text::Tree& root, const Orc::MFTUtils::NonResidentAttributeExtent& extent)
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
};

}  // namespace Text
}  // namespace Orc
