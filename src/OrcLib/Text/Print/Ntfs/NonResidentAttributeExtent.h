//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/Print.h"

#include "MFTUtils.h"

namespace Orc {
namespace Text {

void Print(Tree& root, const MFTUtils::NonResidentAttributeExtent& extent);

}  // namespace Text
}  // namespace Orc
