//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
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
    Network = (One << 2),
    PhysicalDrives = (One << 3),
    MountedVolumes = (One << 4),
    PhysicalMemory = (One << 5),
    CPU = (One << 6),

    System = OperatingSystem | Network | PhysicalDrives | MountedVolumes | PhysicalMemory | CPU,

    ProfileList = (One << 7),

    All = (MinusOne)
};

class SystemIdentity
{

public:
    static HRESULT
    Write(const std::shared_ptr<StructuredOutput::IOutput>& writer, IdentityArea areas = IdentityArea::All);

    static HRESULT
    CurrentProcess(const std::shared_ptr<StructuredOutput::IOutput>& writer, const LPCWSTR elt = L"process");
    static HRESULT CurrentUser(const std::shared_ptr<StructuredOutput::IOutput>& writer, const LPCWSTR elt = L"user");
    static HRESULT System(
        const std::shared_ptr<StructuredOutput::IOutput>& writer,
        const LPCWSTR elt = L"system");  // Includes OS, PhysicalDrives, MountedVolumes,CPU, Memory & Network
    static HRESULT
    OperatingSystem(const std::shared_ptr<StructuredOutput::IOutput>& writer, const LPCWSTR elt = L"operating_system");
    static HRESULT Network(const std::shared_ptr<StructuredOutput::IOutput>& writer, const LPCWSTR elt = L"network");
    static HRESULT
    PhysicalDrives(const std::shared_ptr<StructuredOutput::IOutput>& writer, const LPCWSTR elt = L"physical_drives");
    static HRESULT
    MountedVolumes(const std::shared_ptr<StructuredOutput::IOutput>& writer, const LPCWSTR elt = L"mounted_volumes");
    static HRESULT
    PhysicalMemory(const std::shared_ptr<StructuredOutput::IOutput>& writer, const LPCWSTR elt = L"physical_memory");
    static HRESULT CPU(const std::shared_ptr<StructuredOutput::IOutput>& writer, const LPCWSTR elt = L"cpu");

    static HRESULT
    Profiles(const std::shared_ptr<StructuredOutput::IOutput>& writer, const LPCWSTR elt = L"profile_list");
};

}  // namespace Orc
