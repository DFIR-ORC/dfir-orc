//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "stdafx.h"

#include "Text/Print/Location.h"

namespace Orc {
namespace Text {

namespace detail {

std::vector<std::wstring> GetMountPointList(const Location& location)
{
    std::vector<std::wstring> mountPoints;

    for (const auto& path : location.GetPaths())
    {
        if (location.GetSubDirs().empty())
        {
            mountPoints.push_back(fmt::format(L"\"{}\"", path));
            continue;
        }

        for (const auto& subdir : location.GetSubDirs())
        {
            const auto fullPath = std::filesystem::path(path) / subdir;
            mountPoints.push_back(fullPath);
        }
    }

    return mountPoints;
}

}  // namespace detail

}  // namespace Text
}  // namespace Orc
