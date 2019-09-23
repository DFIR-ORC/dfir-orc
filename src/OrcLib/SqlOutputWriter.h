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

class SqlOutputWriter : public TableOutputExtension
{
public:
    SqlOutputWriter(logger pLog)
        : TableOutputExtension(std::move(pLog), L"orcsql.dll"s, L"ORCSQL_X86DLL"s, L"ORCSQL_X64DLL"s) {};
    ~SqlOutputWriter() {};
};

}  // namespace Orc

#pragma managed(pop)
