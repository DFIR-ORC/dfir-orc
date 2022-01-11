//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/Print.h"

namespace Orc {

class Partition;

namespace Text {

void Print(Tree& root, const Partition& partition);

}  // namespace Text
}  // namespace Orc
