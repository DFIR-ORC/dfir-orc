//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Output/Text/Print.h"

#include "FileFind.h"

namespace Orc {
namespace Text {

template <typename T>
void Print(Orc::Text::Tree<T>& root, const std::shared_ptr<FileFind::SearchTerm>& term)
{
    assert(term);
    Print(root, *term);
}

template <typename T>
void Print(Orc::Text::Tree<T>& root, const FileFind::SearchTerm& term)
{
    Print(root, term.GetDescription());
}

}  // namespace Text
}  // namespace Orc
