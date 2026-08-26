//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "PeImage.h"

#include <cstddef>

#include "Utilities/Error.h"
#include "Utilities/File.h"
#include "Utilities/Log.h"
#include "Utilities/Scope.h"

#include "Fmt/std_error_code.h"
#include "Fmt/std_filesystem.h"

namespace fs = std::filesystem;

namespace {

// IMAGE_OPTIONAL_HEADER32 and IMAGE_OPTIONAL_HEADER64 diverge before Subsystem (ImageBase widens to 8
// bytes, BaseOfData disappears) but the two changes cancel out, so the field sits at the same offset in
// both. That is what lets an x86 capsule adopt the subsystem of an arm64 or x64 payload: the value is read
// at one offset and written at the same one, with no other field involved.
constexpr uint32_t kSubsystemOffset = offsetof(IMAGE_OPTIONAL_HEADER32, Subsystem);

static_assert(
    offsetof(IMAGE_OPTIONAL_HEADER32, Subsystem) == offsetof(IMAGE_OPTIONAL_HEADER64, Subsystem),
    "Subsystem must be at the same offset in PE32 and PE32+ optional headers");

static_assert(kSubsystemOffset == 68, "Unexpected Subsystem offset in the optional header");

constexpr LONG kMaxNtHeadersOffset = 0x10000000;

[[nodiscard]] std::error_code OpenPeFile(const fs::path& path, DWORD desiredAccess, ScopedFileHandle& handle)
{
    // Deny write sharing when the file is opened for writing: nothing else may rewrite the headers while
    // they are being patched.
    const DWORD shareMode =
        (desiredAccess & GENERIC_WRITE) ? FILE_SHARE_READ : FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

    ScopedFileHandle file(
        CreateFileW(path.c_str(), desiredAccess, shareMode, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));

    // CreateFileW reports failure with INVALID_HANDLE_VALUE, not nullptr, so check both; otherwise a
    // missing file would slip past and fail later on an invalid handle with a misleading error.
    if (!file || file.get() == INVALID_HANDLE_VALUE)
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed CreateFileW (path: {}) [{}]", path, ec);
        return ec;
    }

    handle = std::move(file);
    return {};
}

[[nodiscard]] std::error_code ReadHeaders(HANDLE handle, const fs::path& path, PeImageHeaders& headers)
{
    IMAGE_DOS_HEADER dosHeader;
    auto ec = ReadFileAt(handle, 0, &dosHeader, sizeof(dosHeader));
    if (ec)
    {
        Log::Debug(L"Failed ReadFileAt for IMAGE_DOS_HEADER (path: {}) [{}]", path, ec);
        return ec;
    }

    if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE)
    {
        Log::Debug(L"Invalid PE file magic (actual: {}, expected: {})", dosHeader.e_magic, IMAGE_DOS_SIGNATURE);
        return std::make_error_code(std::errc::bad_message);
    }

    if (dosHeader.e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)) || dosHeader.e_lfanew > kMaxNtHeadersOffset)
    {
        Log::Debug(L"Out of range NT headers offset (path: {}, offset: {})", path, dosHeader.e_lfanew);
        return std::make_error_code(std::errc::bad_message);
    }

    DWORD signature = 0;
    ec = ReadFileAt(handle, dosHeader.e_lfanew, &signature, sizeof(signature));
    if (ec)
    {
        Log::Debug(L"Failed ReadFileAt for IMAGE_NT_SIGNATURE (path: {}) [{}]", path, ec);
        return ec;
    }

    if (signature != IMAGE_NT_SIGNATURE)
    {
        Log::Debug(L"Invalid PE file signature (actual: {}, expected: {})", signature, IMAGE_NT_SIGNATURE);
        return std::make_error_code(std::errc::bad_message);
    }

    IMAGE_FILE_HEADER fileHeader;
    ec = ReadFileAt(handle, dosHeader.e_lfanew + sizeof(DWORD), &fileHeader, sizeof(fileHeader));
    if (ec)
    {
        Log::Debug(L"Failed ReadFileAt for IMAGE_FILE_HEADER (path: {}) [{}]", path, ec);
        return ec;
    }

    headers.optionalHeaderOffset =
        static_cast<uint64_t>(dosHeader.e_lfanew) + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
    headers.machine = fileHeader.Machine;
    headers.sizeOfOptionalHeader = fileHeader.SizeOfOptionalHeader;

    return {};
}

// Locate a field of the optional header, rejecting anything that is not a well formed one. Both fields
// this is used for (Subsystem, CheckSum) live at the same offset in PE32 and PE32+, hence a single walker.
[[nodiscard]] std::error_code LocateOptionalHeaderField(
    HANDLE handle,
    const fs::path& path,
    const PeImageHeaders& headers,
    uint32_t fieldOffset,
    uint32_t fieldSize,
    uint64_t& offset)
{
    if (headers.sizeOfOptionalHeader < fieldOffset + fieldSize)
    {
        Log::Debug(
            L"Optional header too small for the requested field (path: {}, size: {}, field offset: {})",
            path,
            headers.sizeOfOptionalHeader,
            fieldOffset);
        return std::make_error_code(std::errc::bad_message);
    }

    WORD magic = 0;
    auto ec = ReadFileAt(handle, headers.optionalHeaderOffset, &magic, sizeof(magic));
    if (ec)
    {
        Log::Debug(L"Failed ReadFileAt for the optional header magic (path: {}) [{}]", path, ec);
        return ec;
    }

    if (magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC && magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        Log::Debug(L"Unsupported optional header magic (path: {}, magic: {:#x})", path, magic);
        return std::make_error_code(std::errc::bad_message);
    }

    offset = headers.optionalHeaderOffset + fieldOffset;
    return {};
}

[[nodiscard]] std::error_code
LocateSubsystem(HANDLE handle, const fs::path& path, const PeImageHeaders& headers, uint64_t& offset)
{
    return LocateOptionalHeaderField(handle, path, headers, kSubsystemOffset, sizeof(WORD), offset);
}

}  // namespace

std::error_code ReadPeImageHeaders(const fs::path& path, PeImageHeaders& headers)
{
    ScopedFileHandle handle;
    auto ec = OpenPeFile(path, GENERIC_READ, handle);
    if (ec)
    {
        return ec;
    }

    return ReadHeaders(handle.get(), path, headers);
}

std::error_code ReadPeSubsystem(const fs::path& path, WORD& subsystem)
{
    ScopedFileHandle handle;
    auto ec = OpenPeFile(path, GENERIC_READ, handle);
    if (ec)
    {
        return ec;
    }

    PeImageHeaders headers;
    ec = ReadHeaders(handle.get(), path, headers);
    if (ec)
    {
        return ec;
    }

    uint64_t offset = 0;
    ec = LocateSubsystem(handle.get(), path, headers, offset);
    if (ec)
    {
        return ec;
    }

    WORD value = 0;
    ec = ReadFileAt(handle.get(), offset, &value, sizeof(value));
    if (ec)
    {
        Log::Debug(L"Failed ReadFileAt for the subsystem (path: {}) [{}]", path, ec);
        return ec;
    }

    subsystem = value;
    return {};
}

std::error_code WritePeSubsystem(const fs::path& path, WORD subsystem)
{
    if (subsystem != IMAGE_SUBSYSTEM_WINDOWS_GUI && subsystem != IMAGE_SUBSYSTEM_WINDOWS_CUI)
    {
        Log::Debug(L"Refusing to write subsystem {} (path: {})", subsystem, path);
        return std::make_error_code(std::errc::invalid_argument);
    }

    ScopedFileHandle handle;
    auto ec = OpenPeFile(path, GENERIC_READ | GENERIC_WRITE, handle);
    if (ec)
    {
        return ec;
    }

    PeImageHeaders headers;
    ec = ReadHeaders(handle.get(), path, headers);
    if (ec)
    {
        return ec;
    }

    uint64_t offset = 0;
    ec = LocateSubsystem(handle.get(), path, headers, offset);
    if (ec)
    {
        return ec;
    }

    WORD current = 0;
    ec = ReadFileAt(handle.get(), offset, &current, sizeof(current));
    if (ec)
    {
        Log::Debug(L"Failed ReadFileAt for the subsystem (path: {}) [{}]", path, ec);
        return ec;
    }

    if (current == subsystem)
    {
        Log::Debug(L"Subsystem already set to '{}' (path: {})", GetSubsystemName(subsystem), path);
        return {};
    }

    // This leaves the header CheckSum describing the image as it was, as every resource update does.
    ec = WriteFileAt(handle.get(), offset, &subsystem, sizeof(subsystem));
    if (ec)
    {
        Log::Debug(L"Failed WriteFileAt for the subsystem (path: {}) [{}]", path, ec);
        return ec;
    }

    Log::Debug(
        L"Subsystem changed from '{}' to '{}' (path: {})",
        GetSubsystemName(current),
        GetSubsystemName(subsystem),
        path);

    return {};
}

const wchar_t* GetSubsystemName(WORD subsystem)
{
    switch (subsystem)
    {
        case IMAGE_SUBSYSTEM_WINDOWS_GUI:
            return L"gui";
        case IMAGE_SUBSYSTEM_WINDOWS_CUI:
            return L"console";
        default:
            return L"unknown";
    }
}
