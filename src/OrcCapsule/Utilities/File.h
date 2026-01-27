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
#include <vector>

#include <windows.h>

[[nodiscard]] std::error_code RemoveFile(const std::wstring& path);

inline [[nodiscard]] std::error_code RemoveFile(const std::filesystem::path& path)
{
    return RemoveFile(path.wstring());
}

[[nodiscard]] std::error_code ReadFileContents(const std::filesystem::path& path, std::vector<uint8_t>& output);

[[nodiscard]] std::error_code ReadFileAt(
    HANDLE handle,
    uint64_t offset,
    LPVOID lpBuffer,
    DWORD nNumberOfBytesToRead,
    LPDWORD lpNumberOfBytesRead = nullptr,
    LPOVERLAPPED lpOverlapped = nullptr);

[[nodiscard]] std::error_code WriteFileContents(const std::filesystem::path& path, const std::vector<uint8_t>& data);
