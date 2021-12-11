//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "SearchTerm.h"

#include "FileFind.h"

namespace Orc {
namespace Text {

void Print(Tree& node, const FileFind::SearchTerm& term)
{
    Print(node, term.GetDescription());
}

}  // namespace Text
}  // namespace Orc
