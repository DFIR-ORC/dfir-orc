//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string_view>
#include <filesystem>

#include <boost/algorithm/string/join.hpp>

#include "Text/Print.h"
#include <Location.h>

namespace Orc {
namespace Text {

namespace detail {
std::vector<std::wstring> GetMountPointList(const Location& location);
}

template <>
struct Printer<Orc::Location>
{
    static void Output(Orc::Text::Tree& node, const Orc::Location& location)
    {
        std::vector<std::wstring> properties;

        properties.push_back(ToString(location.GetType()));

        if (location.IsValid())
        {
            properties.push_back(fmt::format(L"Serial: {:0>16X}", location.SerialNumber()));
        }

        properties.push_back(location.IsValid() ? L"Valid" : L"Invalid");
        properties.push_back(ToString(location.GetFSType()));
        const auto mountPointList = detail::GetMountPointList(location);
        if (mountPointList.size())
        {
            properties.push_back(fmt::format(L"[{}]", boost::join(mountPointList, L", ")));
        }

        Print(node, fmt::format(L"{}  [{}]", location.GetLocation(), boost::join(properties, L", ")));
    }
};

}  // namespace Text
}  // namespace Orc
