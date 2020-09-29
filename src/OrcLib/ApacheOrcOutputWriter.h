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

class ApacheOrcOutputWriter : public TableOutputExtension
{
public:
    ApacheOrcOutputWriter()
        : TableOutputExtension(L"orcapacheorc.dll"s, L"APACHEORC_X86DLL"s, L"APACHEORC_X64DLL"s) {};
    ~ApacheOrcOutputWriter() {};
};

}  // namespace Orc

#pragma managed(pop)
