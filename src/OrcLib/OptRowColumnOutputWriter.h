//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "TableOutputExtension.h"

#pragma managed(push, off)

namespace Orc {

using namespace std::string_literals;

class OptRowColumnOutputWriter : public TableOutputExtension
{
public:
    OptRowColumnOutputWriter(logger pLog)
        : TableOutputExtension(
            std::move(pLog),
            L"orcoptrowcolumn.dll"s,
            L"ORCOPTROWCOLUMN_X86DLL"s,
            L"ORCOPTROWCOLUMN_X64DLL"s) {};
    ~OptRowColumnOutputWriter() {};
};

}  // namespace Orc

#pragma managed(pop)
