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

#include "FSUtils.h"

namespace Orc {
namespace Text {

template <typename T, typename U>
void PrintValue(Orc::Text::Tree<T>& node, const U& name, const std::vector<Orc::Filter>& filters)
{
    if (filters.empty())
    {
        PrintValue(node, name, kStringEmpty);
        return;
    }

    auto filtersNode = node.AddNode("{}:", name);

    for (const auto& filter : filters)
    {
        const auto& action = filter.bInclude ? L"Include" : L"Exclude";

        switch (filter.type)
        {
            case FILEFILTER_EXTBINARY:
                PrintValue(
                    filtersNode, fmt::format(L"{} columns for files with binary extensions", action), filter.intent);
                break;
            case FILEFILTER_EXTSCRIPT:
                PrintValue(
                    filtersNode, fmt::format(L"{} columns for files with script extensions", action), filter.intent);
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
                    filtersNode,
                    fmt::format(
                        L"{} columns for files with custom extensions: {}",
                        action,
                        boost::join(customExtensions, L" ,")),
                    filter.intent);
            }
            break;
            case FILEFILTER_EXTARCHIVE:
                PrintValue(
                    filtersNode, fmt::format(L"{} columns for files with archive extensions", action), filter.intent);
                break;
            case FILEFILTER_PEHEADER:
                PrintValue(
                    filtersNode, fmt::format(L"{} columns for files with valid PE header", action), filter.intent);
                break;
            case FILEFILTER_VERSIONINFO:
                PrintValue(
                    filtersNode,
                    fmt::format(L"{} columns for files with valid VERSION_INFO resource", action),
                    filter.intent);
                break;
            case FILEFILTER_SIZELESS:
                PrintValue(
                    filtersNode,
                    fmt::format(L"{} columns for files smaller than {} bytes", action, filter.filterdata.size.LowPart),
                    filter.intent);
                break;
            case FILEFILTER_SIZEMORE:
                PrintValue(
                    filtersNode,
                    fmt::format(L"{} columns for files bigger than {} bytes", action, filter.filterdata.size.LowPart),
                    filter.intent);
                break;
        }
    }
}

}  // namespace Text
}  // namespace Orc
