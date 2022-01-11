//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "AttributeListEntry.h"

#include <AttributeList.h>

#include "Utils/TypeTraits.h"

namespace Orc {
namespace Text {

void Print(Tree& node, const AttributeListEntry& entry)
{
    std::error_code ec;

    const auto attributeName = Utf16ToUtf8(std::wstring_view(entry.AttributeName(), entry.AttributeNameLength()), ec);
    assert(!ec);

    const auto attributeType = Utf16ToUtf8(std::wstring_view(entry.TypeStr()), ec);
    assert(!ec);

    node.AddWithoutEOL(
        "Type: '{}', Name: '{}', Form: '{}', Id: {:02}",
        attributeType,
        attributeName,
        entry.FormCode() == RESIDENT_FORM ? "R" : "NR",
        entry.Instance());

    if (entry.LowestVCN() > 0)
    {
        node.Append(", LowestVCN={:#018x}\n", entry.LowestVCN());
    }
    else
    {
        node.AddEOL();
    }
}

}  // namespace Text
}  // namespace Orc
