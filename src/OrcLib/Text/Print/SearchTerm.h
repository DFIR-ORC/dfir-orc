//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/Print.h"

#include "FileFind.h"

namespace Orc {
namespace Text {

template <>
struct Printer<FileFind::SearchTerm>
{
    static void Output(Orc::Text::Tree& root, const FileFind::SearchTerm& term)
    {
        Print(root, term.GetDescription());
    }
};

}  // namespace Text
}  // namespace Orc
