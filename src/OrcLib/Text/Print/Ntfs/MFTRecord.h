//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "Text/Tree.h"

namespace Orc {

class MFTRecord;
class VolumeReader;

namespace Text {

void Print(Orc::Text::Tree& root, const MFTRecord& record, const std::shared_ptr<VolumeReader>& volume);

}  // namespace Text
}  // namespace Orc
