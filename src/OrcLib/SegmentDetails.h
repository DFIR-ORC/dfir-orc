//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#pragma once

#include <map>

#pragma managed(push, off)

namespace Orc {

struct SegmentDetails
{
    ULONGLONG mStartOffset;
    ULONGLONG mEndOffset;
    DWORD mSize;
};

using SegmentDetailsMap = std::map<ULONGLONG, SegmentDetails>;
}  // namespace Orc

#pragma managed(pop)
