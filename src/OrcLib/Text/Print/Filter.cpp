//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "FILE_NAME.h"

#include "Text/Print/Intentions.h"
#include "Text/Fmt/ByteQuantity.h"

namespace Orc {
namespace Text {

void PrintValue(
    Tree& node,
    const std::wstring& name,
    const std::vector<Orc::Filter>& filters,
    const ColumnNameDef* pCurCol)
{
    if (filters.empty())
    {
        PrintValue(node, name, kEmptyW);
        return;
    }

    for (const auto& filter : filters)
    {
        const auto& action = filter.bInclude ? L"Include" : L"Exclude";

        switch (filter.type)
        {
            case FILEFILTER_EXTBINARY: {
                // std::wstring ss;
                // fmt::format_to(std::back_inserter(ss), L"foo");
                std::wstring s = fmt::format(L"{} for binary extensions", action);
                PrintValue(node, s, filter.intent, pCurCol);
                break;
            }
            case FILEFILTER_EXTSCRIPT:
                PrintValue(node, fmt::format(L"{} for script extensions", action), filter.intent, pCurCol);
                break;
            case FILEFILTER_EXTCUSTOM: {
                std::vector<std::wstring> customExtensions;
                wchar_t** pCurExt = filter.filterdata.extcustom;
                while (*pCurExt)
                {
                    customExtensions.push_back({*pCurExt});
                    pCurExt++;
                }

                std::sort(std::begin(customExtensions), std::end(customExtensions));

                PrintValue(
                    node,
                    fmt::format(L"{} for custom extensions: {}", action, fmt::join(customExtensions, L", ")),
                    filter.intent,
                    pCurCol);
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
}  // namespace Text

}  // namespace Text
}  // namespace Orc
