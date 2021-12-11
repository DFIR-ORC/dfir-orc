//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "LocationSet.h"

#include <algorithm>
#include <string_view>
#include <filesystem>

#include "Text/Print/Location.h"

namespace {

template <typename InputIt>
bool HasSameAttributes(InputIt first, InputIt last)
{
    if (std::distance(first, last) < 2)
    {
        return true;
    }

    return std::all_of(first, last, [reference = *first](const Orc::Location* current) {
        return reference->IsValid() == current->IsValid() && reference->GetFSType() == current->GetFSType();
    });
}

}  // namespace

namespace Orc {
namespace Text {

// TODO: use FormatKey
void Print(Tree& root, const LocationSet& locationSet)
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
        const auto constantAttributes = ::HasSameAttributes(std::cbegin(locations), std::cend(locations));

        std::wstring serialNodeString;
        if (constantAttributes)
        {
            const auto location = *locations.cbegin();

            std::vector<std::wstring> attributes;
            attributes.push_back(serialW);
            attributes.push_back(location->IsValid() ? L"Valid" : L"Invalid");
            attributes.push_back(ToString(location->GetFSType()));

            serialNodeString = fmt::format(L"Volume: {}", fmt::join(attributes, L", "));
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
            const auto mountPointList = GetMountPointList(*location);
            if (mountPointList.size())
            {
                mountPoints = fmt::format(L"  [{}]", fmt::join(mountPointList, L", "));
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

}  // namespace Text
}  // namespace Orc
