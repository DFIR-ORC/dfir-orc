//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "FILE_NAME.h"

namespace Orc {
namespace Text {

void Print(Tree& node, const FILE_NAME& file_name)
{
    const auto parentFRN = NtfsFullSegmentNumber(&file_name.ParentDirectory);
    const auto& creation = *(reinterpret_cast<const FILETIME*>(&file_name.Info.CreationTime));
    const auto& lastModification = *(reinterpret_cast<const FILETIME*>(&file_name.Info.LastModificationTime));
    const auto& lastAccess = *(reinterpret_cast<const FILETIME*>(&file_name.Info.LastAccessTime));
    const auto& lastChange = *(reinterpret_cast<const FILETIME*>(&file_name.Info.LastChangeTime));

    constexpr std::array fileFlags = {
        L"FILE_NAME_POSIX", L"FILE_NAME_WIN32", L"FILE_NAME_DOS83", L"FILE_NAME_WIN32|FILE_NAME_DOS83"};

    const auto flags = file_name.Flags < fileFlags.size() ? fileFlags[file_name.Flags] : L"N/A";

    PrintValue(node, L"Name", std::wstring_view(file_name.FileName, file_name.FileNameLength));
    PrintValue(node, L"Parent FRN", fmt::format("{:#016x}", parentFRN));
    PrintValue(node, L"Creation", creation);
    PrintValue(node, L"Last modification", lastModification);
    PrintValue(node, L"Last access", lastAccess);
    PrintValue(node, L"Last change", lastChange);
    PrintValue(node, L"Flags", flags);
}

}  // namespace Text
}  // namespace Orc
