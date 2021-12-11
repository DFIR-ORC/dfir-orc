//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <OutputSpec.h>

#include "Text/Print.h"

namespace Orc {
namespace Text {

void Print(Tree& node, const OutputSpec::Upload& upload);

void Print(Tree& node, const OutputSpec& output);

}  // namespace Text
}  // namespace Orc
