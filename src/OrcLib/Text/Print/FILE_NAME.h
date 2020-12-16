//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Output/Text/Print.h"

#include <NtfsDataStructures.h>

namespace Orc {
namespace Text {

template <>
struct Printer<FILE_NAME>
{
    template <typename T>
    static void Output(Orc::Text::Tree<T>& node, const FILE_NAME& value)
    {
        const auto parentFRN = NtfsFullSegmentNumber(&file_name.ParentDirectory);
        const auto& creation = *(reinterpret_cast<const FILETIME*>(&file_name.Info.CreationTime));
        const auto& lastModification = *(reinterpret_cast<const FILETIME*>(&file_name.Info.LastModificationTime));
        const auto& lastAccess = *(reinterpret_cast<const FILETIME*>(&file_name.Info.LastAccessTime));
        const auto& lastChange = *(reinterpret_cast<const FILETIME*>(&file_name.Info.LastChangeTime));

        auto node = root.AddNode(file_name);
        node.Add("Parent directory FRN: {:#016x}", parentFRN);
        node.Add("CreationTime: {}", creation);
        node.Add("LastModificationTime: {}", lastModification);
        node.Add("LastAccessTime: {}", lastAccess);
        node.Add("LastChangeTime: {}", lastChange);
    }
};

}  // namespace Text
}  // namespace Orc
