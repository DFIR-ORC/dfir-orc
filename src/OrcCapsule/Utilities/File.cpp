//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "File.h"

#include <windows.h>

#include "Utilities/Log.h"
#include "Utilities/Error.h"
#include "Utilities/Scope.h"

#include "Fmt/std_error_code.h"
#include "Fmt/std_filesystem.h"

namespace fs = std::filesystem;

std::error_code RemoveFile(const std::wstring& path)
{
    std::error_code ec;

    fs::remove(path, ec);
    if (!ec)
    {
        return {};
    }

    Log::Debug(L"Schedule file to be removed at reboot as it has failed (path: {}) [{}]", path, ec);
    ec.clear();

    if (!MoveFileExW(path.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT))
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed MoveFileExW (path: {}) [{}]", path, ec);
        return ec;
    }

    return {};
}

std::error_code ReadFileContents(const fs::path& path, std::vector<uint8_t>& output)
{
    std::error_code ec;

    ScopedFileHandle hFile(CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));

    if (!hFile || hFile.get() == INVALID_HANDLE_VALUE)
    {
        ec = LastWin32Error();
        Log::Error(L"Failed CreateFileW (path: {}) [{}]", path, ec);
        return ec;
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile.get(), &fileSize))
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed GetFileSizeEx (path: {}) [{}]", path, ec);
        return ec;
    }

    try
    {
        output.resize(static_cast<size_t>(fileSize.QuadPart));
    }
    catch (const std::bad_alloc& e)
    {
        Log::Critical("Failed to allocate {} bytes: {}", fileSize.QuadPart, e.what());
        return std::make_error_code(std::errc::not_enough_memory);
    }

    DWORD bytesRead = 0;
    if (!ReadFile(hFile.get(), output.data(), static_cast<DWORD>(output.size()), &bytesRead, nullptr))
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed ReadFile (path: {}, length: {}) [{}]", path, output.size(), ec);
        return ec;
    }

    if (bytesRead != output.size())
    {
        Log::Debug(L"Failed ReadFile (path: {}, read length: {}, expected length: {})", path, bytesRead, output.size());
        return std::make_error_code(std::errc::io_error);
    }

    return {};
}

std::error_code WriteFileContents(const fs::path& path, const std::vector<uint8_t>& data)
{
    ScopedFileHandle hFile(
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));

    if (!hFile || hFile.get() == INVALID_HANDLE_VALUE)
    {
        std::error_code ec = LastWin32Error();
        Log::Error(L"Failed CreateFileW (path: {}) [{}]", path, ec);
        return ec;
    }

    DWORD bytesWritten = 0;
    if (!WriteFile(hFile.get(), data.data(), static_cast<DWORD>(data.size()), &bytesWritten, nullptr))
    {
        std::error_code ec = LastWin32Error();
        Log::Error(L"Failed WriteFile (path: {}) [{}]", path, ec);
        return ec;
    }

    if (bytesWritten != data.size())
    {
        Log::Debug(
            L"Failed WriteFile (path: {}, write length: {}, expected length: {})", path, bytesWritten, data.size());
        return std::make_error_code(std::errc::io_error);
    }

    return {};
}

std::error_code ReadFileAt(
    HANDLE handle,
    uint64_t offset,
    LPVOID lpBuffer,
    DWORD nNumberOfBytesToRead,
    LPDWORD lpNumberOfBytesRead,
    LPOVERLAPPED lpOverlapped)
{
    std::error_code ec;

    LARGE_INTEGER llDistanceToMove;
    llDistanceToMove.QuadPart = offset;

    DWORD fallbackNumberOfBytesRead = 0;
    if (!lpNumberOfBytesRead)
    {
        lpNumberOfBytesRead = &fallbackNumberOfBytesRead;
    }

    LARGE_INTEGER llNewFilePointer;
    if (!SetFilePointerEx(handle, llDistanceToMove, &llNewFilePointer, FILE_BEGIN))
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed SetFilePointerEx (offset: {}) [{}]", offset, ec);
        return ec;
    }

    if (!ReadFile(handle, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped))
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed ReadFile (offset: {}, size: {}) [{}]", offset, nNumberOfBytesToRead, ec);
        return ec;
    }

    if (*lpNumberOfBytesRead != nNumberOfBytesToRead)
    {
        Log::Debug(
            L"Incomplete ReadFile (actual: {}, expected: {}, offset: {})",
            *lpNumberOfBytesRead,
            nNumberOfBytesToRead,
            offset);
        return std::make_error_code(std::errc::message_size);
    }

    return {};
}
