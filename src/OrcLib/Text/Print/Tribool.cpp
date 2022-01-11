//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Tribool.h"

namespace Orc {
namespace Text {

void Print(Tree& node, const boost::logic::tribool& value)
{
    std::wstring_view valueString;

    if (value)
    {
        valueString = L"On";
    }
    else if (!value)
    {
        valueString = L"Off";
    }
    else
    {
        valueString = L"Unset";
    }

    Print(node, valueString);
}

}  // namespace Text
}  // namespace Orc
