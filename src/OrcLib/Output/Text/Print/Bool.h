//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Output/Text/Print.h"

namespace Orc {
namespace Text {

template <typename T>
void Print(Orc::Text::Tree<T>& root, const bool& value)
{
    Print(root, value ? L"On" : L"Off");
}

}  // namespace Text
}  // namespace Orc
