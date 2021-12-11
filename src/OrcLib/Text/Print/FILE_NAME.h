//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/Print.h"

#include <NtfsDataStructures.h>

namespace Orc {
namespace Text {

void Print(Tree& node, const FILE_NAME& file_name);

}  // namespace Text
}  // namespace Orc
