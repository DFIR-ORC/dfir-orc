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

/// @brief The parts of a PE file's DOS/NT headers the capsule cares about.
struct PeImageHeaders
{
    /// File offset of IMAGE_OPTIONAL_HEADER32/IMAGE_OPTIONAL_HEADER64.
    uint64_t optionalHeaderOffset = 0;

    /// IMAGE_FILE_HEADER::Machine (IMAGE_FILE_MACHINE_*), i.e. the image's target architecture.
    WORD machine = 0;

    /// IMAGE_FILE_HEADER::SizeOfOptionalHeader, used to bound reads into the optional header.
    WORD sizeOfOptionalHeader = 0;
};

/// @brief Read the DOS and file headers of a PE file.
/// @return errc::bad_message when the file is not a PE image, or its headers are inconsistent.
[[nodiscard]] std::error_code ReadPeImageHeaders(const std::filesystem::path& path, PeImageHeaders& headers);

/// @brief Read IMAGE_OPTIONAL_HEADER::Subsystem (IMAGE_SUBSYSTEM_*) from a PE file.
[[nodiscard]] std::error_code ReadPeSubsystem(const std::filesystem::path& path, WORD& subsystem);

/// @brief Overwrite IMAGE_OPTIONAL_HEADER::Subsystem of a PE file in place.
///
/// Only that single WORD is rewritten: the machine type, the optional header magic (PE32 vs PE32+) and
/// every other header field keep their value, so the image stays the architecture it was built for.
///
/// @param subsystem IMAGE_SUBSYSTEM_WINDOWS_GUI or IMAGE_SUBSYSTEM_WINDOWS_CUI. Any other value is
///                  rejected as invalid_argument: those two are the only ones a capsule can be flipped
///                  between without changing how the loader starts the image.
[[nodiscard]] std::error_code WritePeSubsystem(const std::filesystem::path& path, WORD subsystem);

/// @brief Human readable name of an IMAGE_SUBSYSTEM_* value, for logs and usage output.
[[nodiscard]] const wchar_t* GetSubsystemName(WORD subsystem);
