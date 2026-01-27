//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <system_error>
#include <filesystem>

constexpr uint16_t kPROCESSOR_ARCHITECTURE_ARM64 = 12;
constexpr uint16_t kIMAGE_FILE_MACHINE_ARM64 = 0xAA64;

enum class Architecture
{
    None,
    X86,
    X86_XP,
    X64_XP,
    X64,
    ARM64,

    Count
};

inline size_t ArchitectureCount()
{
    return static_cast<size_t>(Architecture::Count) - 1;
}

[[nodiscard]] Architecture ParseArchitecture(std::wstring_view arch);

[[nodiscard]] const wchar_t* GetArchitectureName(Architecture arch);
