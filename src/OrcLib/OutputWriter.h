//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <string>

#pragma managed(push, off)

constexpr auto MAX_COLUMN_NAME = 35;

namespace Orc {

struct OutputOptions
{
    virtual ~OutputOptions() {};
};

class OutputWriter
{
public:
    virtual ~OutputWriter() {}
};

}  // namespace Orc

#pragma managed(pop)
