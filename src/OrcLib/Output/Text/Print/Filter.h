//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string_view>

#include "Output/Text/Print.h"
#include "Output/Text/Print/Intentions.h"
#include "Output/Text/Fmt/ByteQuantity.h"

#include "FSUtils.h"

namespace Orc {
namespace Text {

template <typename T, typename U>
void PrintValue(
    Orc::Text::Tree<T>& node,
    const U& name,
    const std::vector<Orc::Filter>& filters,
    const ColumnNameDef* pCurCol)
{
    if (filters.empty())
    {
        PrintValue(node, name, kStringEmpty);
        return;
    }

    for (const auto& filter : filters)
    {
        const auto& action = filter.bInclude ? L"Include" : L"Exclude";

        switch (filter.type)
        {
            case FILEFILTER_EXTBINARY:
                PrintValue(node, fmt::format(L"{} for binary extensions", action), filter.intent, pCurCol);
                break;
            case FILEFILTER_EXTSCRIPT:
                PrintValue(node, fmt::format(L"{} for script extensions", action), filter.intent, pCurCol);
                break;
            case FILEFILTER_EXTCUSTOM: {
                std::vector<std::wstring> customExtensions;
                wchar_t** pCurExt = filter.filterdata.extcustom;
                while (*pCurExt)
                {
                    customExtensions.push_back({*pCurExt});
                }

                std::sort(std::begin(customExtensions), std::end(customExtensions));

                PrintValue(
                    node,
                    fmt::format(L"{} for custom extensions: {}", action, boost::join(customExtensions, L" ,")),
                    filter.intent);
            }
            break;
            case FILEFILTER_EXTARCHIVE:
                PrintValue(node, fmt::format(L"{} for archive extensions", action), filter.intent, pCurCol);
                break;
            case FILEFILTER_PEHEADER:
                PrintValue(node, fmt::format(L"{} for PE header", action), filter.intent, pCurCol);
                break;
            case FILEFILTER_VERSIONINFO:
                PrintValue(node, fmt::format(L"{} for VERSION_INFO resource", action), filter.intent, pCurCol);
                break;
            case FILEFILTER_SIZELESS:
                PrintValue(
                    node,
                    fmt::format(L"{} for files <{}", action, Traits::ByteQuantity(filter.filterdata.size.QuadPart)),
                    filter.intent,
                    pCurCol);
                break;
            case FILEFILTER_SIZEMORE:
                PrintValue(
                    node,
                    fmt::format(L"{} for files >{}", action, Traits::ByteQuantity(filter.filterdata.size.QuadPart)),
                    filter.intent,
                    pCurCol);
                break;
        }
    }
}

}  // namespace Text
}  // namespace Orc
