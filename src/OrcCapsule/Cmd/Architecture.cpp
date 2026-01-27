//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Architecture.h"

#include "Utilities/Strings.h"

Architecture ParseArchitecture(std::wstring_view arch)
{
    if (EqualsIgnoreCase(arch, L"x64"))
        return Architecture::X64;
    if (EqualsIgnoreCase(arch, L"x86"))
        return Architecture::X86;
    if (EqualsIgnoreCase(arch, L"x64-xp"))
        return Architecture::X64_XP;
    if (EqualsIgnoreCase(arch, L"x86-xp"))
        return Architecture::X86_XP;
    if (EqualsIgnoreCase(arch, L"arm64"))
        return Architecture::ARM64;
    return Architecture::None;
}

const wchar_t* GetArchitectureName(Architecture arch)
{
    switch (arch)
    {
        case Architecture::X64:
            return L"x64";
        case Architecture::X86:
            return L"x86";
        case Architecture::X64_XP:
            return L"x64-xp";
        case Architecture::X86_XP:
            return L"x86-xp";
        case Architecture::ARM64:
            return L"arm64";
        default:
            return L"<unknown>";
    }
}
