//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <boost/logic/tribool.hpp>

#include "Text/Print.h"

namespace Orc {
namespace Text {

void Print(Tree& node, const boost::logic::tribool& value);

}  // namespace Text
}  // namespace Orc
