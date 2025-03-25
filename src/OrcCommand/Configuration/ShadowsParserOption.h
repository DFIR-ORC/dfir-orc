//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string>
#include <optional>
#include <system_error>

#include "Filesystem/Ntfs/ShadowCopy/ParserType.h"

namespace Orc {

void ParseShadowsParserOption(
    std::wstring_view shadowsParser,
    std::optional<Ntfs::ShadowCopy::ParserType>& parser,
    std::error_code& ec);

}  // namespace Orc
