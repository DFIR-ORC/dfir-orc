//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "stdafx.h"

#include "Output/Text/Tree.h"

#include <assert.h>

namespace Orc {
namespace Text {

template <>
const std::basic_string_view<char>& GetIndent(uint16_t level)
{
    _ASSERT(level < kIndentLevels.size());
    return kIndentLevels[level];
}

template <>
const std::basic_string_view<wchar_t>& GetIndent(uint16_t level)
{
    _ASSERT(level < kIndentLevelsW.size());
    return kIndentLevelsW[level];
}

}  // namespace Text
}  // namespace Orc
