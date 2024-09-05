//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2024 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Utils/MapFile.h"

#include "Utils/Result.h"
#include "Utils/Guard.h"

using namespace Orc;

namespace {

Result<uint64_t> GetFileSize(HANDLE hFile)
{
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize))
    {
        return LastWin32Error();
    }

    return fileSize.QuadPart;
}

}  // namespace

namespace Orc {

Result<std::vector<uint8_t>> MapFile(const std::filesystem::path& path)
{
    Guard::FileHandle file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (!file.IsValid())
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed CreateFile [{}]", ec);
        return ec;
    }

    auto bytesToRead = GetFileSize(file.value());
    if (bytesToRead.has_error())
    {
        Log::Debug("Failed GetFileSizeEx [{}]", bytesToRead.error());
        return bytesToRead.error();
    }

    if (*bytesToRead > UINT32_MAX)
    {
        Log::Debug("File is too big");
        return std::make_error_code(std::errc::no_buffer_space);
    }

    std::vector<uint8_t> buffer;

    try
    {
        buffer.resize(*bytesToRead);
    }
    catch (const std::exception& e)
    {
        Log::Debug("Failed to resize buffer [{}]", e.what());
        return std::make_error_code(std::errc::no_buffer_space);
    }

    DWORD bytesRead = 0;
    if (!ReadFile(file.value(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, NULL))
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed ReadFile [{}]", ec);
        return ec;
    }

    return buffer;
}

}  // namespace Orc
