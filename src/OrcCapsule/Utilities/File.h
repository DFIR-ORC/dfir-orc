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

#include "Utilities/Scope.h"

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

[[nodiscard]] std::error_code WriteFileAt(
    HANDLE handle,
    uint64_t offset,
    LPCVOID lpBuffer,
    DWORD nNumberOfBytesToWrite,
    LPDWORD lpNumberOfBytesWritten = nullptr,
    LPOVERLAPPED lpOverlapped = nullptr);

[[nodiscard]] std::error_code WriteFileContents(const std::filesystem::path& path, const std::vector<uint8_t>& data);

/// @brief Open an existing file with a delete-on-close handle that keeps it alive without holding write access.
///
/// The kernel deletes the file when its last handle closes, with no privilege and no reboot (works from
/// Windows XP onward). The handle is opened GENERIC_READ | DELETE and shares read, write and delete so the
/// image loader can map and run the file while the delete is pending. It deliberately holds no write access:
/// CreateProcessW's loader opens the image without FILE_SHARE_WRITE and would reject a retained writable
/// handle with ERROR_SHARING_VIOLATION. Write the file first (e.g. with WriteFileContents), then keep this
/// handle open for as long as the file must exist and close it to trigger removal.
[[nodiscard]] std::error_code OpenDeleteOnCloseFile(const std::filesystem::path& path, ScopedFileHandle& handle);
