//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Output/Text/Print.h"

#include "FSUtils.h"

namespace Orc {
namespace Text {

template <typename T, typename N>
void PrintValue(Orc::Text::Tree<T>& root, const N& name, Orc::Intentions intentions, const ColumnNameDef* pCurCol)
{
    std::vector<std::wstring> columns;

    while (pCurCol->dwIntention != Intentions::FILEINFO_NONE)
    {
        if ((intentions & pCurCol->dwIntention) == pCurCol->dwIntention)
        {
            columns.push_back(pCurCol->szColumnName);
        }

        pCurCol++;
    }

    std::sort(std::begin(columns), std::end(columns));
    PrintValues(root, name, columns);
}

}  // namespace Text
}  // namespace Orc
