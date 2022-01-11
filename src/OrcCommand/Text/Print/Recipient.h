//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/Print.h"

#include "Command/WolfLauncher/WolfExecution.h"

namespace Orc {
namespace Text {

void Print(Tree& node, const Orc::Command::Wolf::WolfExecution::Recipient& recipient);

}  // namespace Text
}  // namespace Orc
