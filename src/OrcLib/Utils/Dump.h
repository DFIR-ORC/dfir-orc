//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <fstream>
#include <filesystem>

namespace Orc {

template <typename T>
void Dump(const T& container, const std::filesystem::path& output)
{
    std::ofstream ofs(output, std::ios_base::binary);
    std::copy(std::cbegin(container), std::cend(container), std::ostreambuf_iterator<char>(ofs));
}

}  // namespace Orc
