//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Location.h"

#include <string_view>
#include <filesystem>

namespace Orc {
namespace Text {

void Print(Tree& node, const Orc::Location& location)
{
    std::vector<std::wstring> properties;

    properties.push_back(ToString(location.GetType()));

    if (location.IsValid())
    {
        properties.push_back(fmt::format(L"Serial: {:0>16X}", location.SerialNumber()));
    }

    properties.push_back(location.IsValid() ? L"Valid" : L"Invalid");
    properties.push_back(ToString(location.GetFSType()));
    const auto mountPointList = GetMountPointList(location);
    if (mountPointList.size())
    {
        properties.push_back(fmt::format(L"[{}]", fmt::join(mountPointList, L", ")));
    }

    Print(node, fmt::format(L"{}  [{}]", location.GetLocation(), fmt::join(properties, L", ")));
}

}  // namespace Text
}  // namespace Orc
