//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string>
#include <system_error>

namespace Orc {

enum class WimlibAlgorithm
{
    kUnknown = -1,
    kNone = 0,
    kXpress,
    kLzx,
    kLzms
};

std::string_view ToString(WimlibAlgorithm algorithm);

WimlibAlgorithm ToWimlibAlgorithm(const std::string& algorithm, std::error_code& ec);

}  // namespace Orc
