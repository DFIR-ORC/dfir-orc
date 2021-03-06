//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright � 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/Print.h"

namespace Orc {
namespace Text {

template <>
struct Printer<bool>
{
    template <typename T>
    static void Output(Orc::Text::Tree<T>& node, bool value)
    {
        Print(node, value ? L"On" : L"Off");
    }
};

}  // namespace Text
}  // namespace Orc
