//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <algorithm>
#include <string_view>
#include <filesystem>

#include <boost/algorithm/string/join.hpp>

#include <LocationSet.h>

#include "Text/Print/Location.h"

namespace Orc {
namespace Text {

namespace detail {

template <typename InputIt>
bool HasSameAttributes(InputIt first, InputIt last)
{
    if (std::distance(first, last) < 2)
    {
        return true;
    }

    return std::all_of(first, last, [reference = *first](const Location* current) {
        return reference->IsValid() == current->IsValid() && reference->GetFSType() == current->GetFSType();
    });
}

}  // namespace detail

template <>
struct Printer<LocationSet>
{
    template <typename T>
    static void Output(Orc::Text::Tree<T>& root, const LocationSet& locationSet)
    {
        using SerialNumber = ULONGLONG;
        std::map<SerialNumber, std::set<const Location*>> locationsBySerial;
        for (const auto& [serial, location] : locationSet.GetLocations())
        {
            assert(location);
            if (location->IsValid())
            {
                locationsBySerial[location->SerialNumber()].insert(location.get());
            }
            else
            {
                locationsBySerial[ULLONG_MAX].insert(location.get());
            }
        }

        for (const auto& [serial, locations] : locationsBySerial)
        {
            const std::wstring serialW =
                serial != ULLONG_MAX ? fmt::format(L"{:0>16X}", serial) : fmt::format(L"{:>16}", L"<Invalid serial>");

            // Verify for each volume that informations are consitent/identical to display them once or always
            const auto constantAttributes = detail::HasSameAttributes(std::cbegin(locations), std::cend(locations));

            std::wstring serialNodeString;
            if (constantAttributes)
            {
                const auto location = *locations.cbegin();

                std::vector<std::wstring> attributes;
                attributes.push_back(serialW);
                attributes.push_back(location->IsValid() ? L"Valid" : L"Invalid");
                attributes.push_back(ToString(location->GetFSType()));

                serialNodeString = fmt::format(L"Volume: {}", boost::join(attributes, L", "));
            }
            else
            {
                serialNodeString = fmt::format(L"Volume: {}", serialW);
            }

            auto serialNode = root.AddNode(serialNodeString);

            for (const auto location : locations)
            {
                if (!constantAttributes)
                {
                    PrintValue(serialNode, L"", *location);
                    continue;
                }

                std::wstring mountPoints;
                const auto mountPointList = detail::GetMountPointList(*location);
                if (mountPointList.size())
                {
                    mountPoints = fmt::format(L"  [{}]", boost::join(mountPointList, L", "));
                }

                serialNode.Add(
                    L"{:<34}  {}{}", fmt::format(L"{}:", location->GetType()), location->GetLocation(), mountPoints);

                // TODO: Having access to FormatTo would be good. A version without header, conversion to FmtArg0
                // encoding, conversion to container's encoding auto value = FormatTo(L"{:<34}  {}{}",
                // fmt::format(L"{}:", location->GetType()), location->GetLocation(), mountPoints); Print(node, value);
            }

            serialNode.AddEmptyLine();
        }
    }
};

template <typename T, typename U>
void PrintValue(Orc::Text::Tree<T>& root, const U& name, const LocationSet& locationSet)
{
    if (locationSet.GetLocations().empty())
    {
        PrintValue(root, name, kEmpty);
        return;
    }

    auto node = root.AddNode(name);
    Print(node, locationSet);
}

}  // namespace Text
}  // namespace Orc
