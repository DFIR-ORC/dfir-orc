//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/Print/Location.h"
#include <LocationSet.h>

namespace Orc {
namespace Text {

// TODO: use FormatKey
void Print(Tree& root, const LocationSet& locationSet);

}  // namespace Text
}  // namespace Orc
