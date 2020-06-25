//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "StructuredOutputWriter.h"

#pragma managed(push, off)

namespace Orc {

    
static constexpr const unsigned __int64 One = 1;
static constexpr const unsigned __int64 MinusOne = 0xFFFFFFFFFFFFFFFF;

enum IdentityArea : DWORDLONG
{
    None = 0,

    Process = (One),

    OperatingSystem = (One << 1),
    Network         = (One << 2),

    System = OperatingSystem | Network,

    ProfileList = (One << 3),

    All = (MinusOne)
};

class SystemIdentity
{

public:

    static HRESULT Write(const std::shared_ptr<StructuredOutput::IWriter>& writer, IdentityArea areas = IdentityArea::All); 

    static HRESULT Process(const std::shared_ptr<StructuredOutput::IWriter>& writer, const LPCWSTR elt = L"process");
    static HRESULT System(const std::shared_ptr<StructuredOutput::IWriter>& writer, const LPCWSTR elt = L"system"); // Includes OS & Network
    static HRESULT
    OperatingSystem(const std::shared_ptr<StructuredOutput::IWriter>& writer, const LPCWSTR elt = L"operating_system");
    static HRESULT Network(const std::shared_ptr<StructuredOutput::IWriter>& writer, const LPCWSTR elt = L"network");
    static HRESULT
    ProfileList(const std::shared_ptr<StructuredOutput::IWriter>& writer, const LPCWSTR elt = L"profiles");

};

}

