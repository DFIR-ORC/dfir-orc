//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <string>

namespace rcedit {

enum class CompressionType
{
    kNone = 0,
    k7zip
};

std::string ToString(CompressionType type);

std::istream& operator>>( std::istream& in, CompressionType& type );

}  // namespace rcedit
