//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/Print.h"

#include <FileFind.h>

namespace Orc {
namespace Text {

void Print(Tree& node, const FileFind::SearchTerm& term);

}  // namespace Text
}  // namespace Orc
