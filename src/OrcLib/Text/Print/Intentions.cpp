//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Intentions.h"

namespace Orc {
namespace Text {

void PrintValue(Tree& node, const std::wstring& key, Orc::Intentions intentions, const ColumnNameDef* pCurCol)
{
    std::vector<std::wstring> columns;

    while (pCurCol->dwIntention != Intentions::FILEINFO_NONE)
    {
        if (HasFlag(intentions, pCurCol->dwIntention))
        {
            columns.push_back(pCurCol->szColumnName);
        }

        pCurCol++;
    }

    std::sort(std::begin(columns), std::end(columns));
    PrintValues(node, key, columns);
}

}  // namespace Text
}  // namespace Orc
