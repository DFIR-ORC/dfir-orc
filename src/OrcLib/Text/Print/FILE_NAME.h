//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/Print.h"

#include <NtfsDataStructures.h>

namespace Orc {
namespace Text {

template <>
struct Printer<FILE_NAME>
{
    template <typename T>
    static void Output(Orc::Text::Tree<T>& root, const FILE_NAME& file_name)
    {
        const auto parentFRN = NtfsFullSegmentNumber(&file_name.ParentDirectory);
        const auto& creation = *(reinterpret_cast<const FILETIME*>(&file_name.Info.CreationTime));
        const auto& lastModification = *(reinterpret_cast<const FILETIME*>(&file_name.Info.LastModificationTime));
        const auto& lastAccess = *(reinterpret_cast<const FILETIME*>(&file_name.Info.LastAccessTime));
        const auto& lastChange = *(reinterpret_cast<const FILETIME*>(&file_name.Info.LastChangeTime));

        constexpr std::array fileFlags = {
            "FILE_NAME_POSIX", "FILE_NAME_WIN32", "FILE_NAME_DOS83", "FILE_NAME_WIN32|FILE_NAME_DOS83"};

        const auto flags = file_name.Flags < fileFlags.size() ? fileFlags[file_name.Flags] : "N/A";

        PrintValue(root, L"Name", std::wstring_view(file_name.FileName, file_name.FileNameLength));
        PrintValue(root, "Parent FRN", fmt::format("{:#016x}", parentFRN));
        PrintValue(root, "Creation", creation);
        PrintValue(root, "Last modification", lastModification);
        PrintValue(root, "Last access", lastAccess);
        PrintValue(root, "Last change", lastChange);
        PrintValue(root, "Flags", flags);
    }
};

}  // namespace Text
}  // namespace Orc
