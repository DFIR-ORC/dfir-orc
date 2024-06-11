//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2024 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <filesystem>
#include <vector>

#include "Utils/Result.h"

namespace Orc {

Result<std::vector<uint8_t>> MapFile(const std::filesystem::path& path);

}  // namespace Orc
