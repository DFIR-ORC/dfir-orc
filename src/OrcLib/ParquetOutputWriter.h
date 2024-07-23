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

class ParquetOutputWriter : public TableOutputExtension
{
public:
    ParquetOutputWriter()
        : TableOutputExtension(L"orcparquet.dll"s, L"ORCPARQUET_X86DLL"s, L"ORCPARQUET_X64DLL"s, L"ORCPARQUET_ARM64DLL"s) {};
    ~ParquetOutputWriter() {};
};

}  // namespace Orc

#pragma managed(pop)
