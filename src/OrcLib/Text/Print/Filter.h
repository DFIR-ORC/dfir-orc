//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string_view>
#include <vector>

#include "Text/Print.h"
#include "FSUtils.h"

namespace Orc {
namespace Text {

void PrintValue(
    Tree& node,
    const std::wstring& name,
    const std::vector<Orc::Filter>& filters,
    const ColumnNameDef* pCurCol);

}  // namespace Text
}  // namespace Orc
