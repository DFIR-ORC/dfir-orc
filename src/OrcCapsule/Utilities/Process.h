//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <filesystem>
#include <system_error>

#include <windows.h>

[[nodiscard]] std::error_code GetCurrentProcessExecutable(std::filesystem::path& path);

[[nodiscard]] std::error_code PerformSelfUpdate(const std::filesystem::path& replacementExecutable);

[[nodiscard]] std::error_code
WaitChildProcess(const PROCESS_INFORMATION& pi, const std::filesystem::path& executable, DWORD& childExitCode);

// Lack of abstraction
[[nodiscard]] std::error_code
RelocateOrcFromNetwork(const std::filesystem::path& outputDirectory, const std::vector<std::wstring>& childArgs);
