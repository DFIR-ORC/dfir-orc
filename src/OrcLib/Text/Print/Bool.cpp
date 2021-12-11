//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Bool.h"

namespace Orc {
namespace Text {

void Print(Tree& node, bool value)
{
    node.Add(L"{}", value ? L"On" : L"Off");
}

}  // namespace Text
}  // namespace Orc
