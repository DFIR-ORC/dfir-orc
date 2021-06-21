//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

namespace Orc {

enum class SeekDirection
{
    kBegin,
    kCurrent,
    kEnd
};

inline int ToWin32(SeekDirection direction)
{
    return std::underlying_type_t<SeekDirection>(direction);
}

}  // namespace Orc
