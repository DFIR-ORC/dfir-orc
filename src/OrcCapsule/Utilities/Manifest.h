//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <vector>

#include <windows.h>

/// @brief UAC level declared by a PE manifest's <requestedExecutionLevel> element.
enum class ExecutionLevel
{
    None,  // No manifest, no requestedExecutionLevel, or an unrecognized level
    AsInvoker,
    HighestAvailable,
    RequireAdministrator
};

[[nodiscard]] const wchar_t* GetExecutionLevelName(ExecutionLevel level);

/// @brief Parse a manifest 'level' attribute value (ASCII, case-insensitive).
/// @return ExecutionLevel::None when the value is not one of the three documented levels.
[[nodiscard]] ExecutionLevel ParseExecutionLevel(std::string_view value);

/// @brief Read the requested execution level from a manifest blob.
///
/// Fail-closed by design: anything ambiguous (UTF-16 blob, no <requestedExecutionLevel> element, more
/// than one of them, missing 'level' attribute) yields ExecutionLevel::None rather than a guess, so the
/// caller keeps its own level instead of adopting a wrong one.
///
/// @param manifest Raw RT_MANIFEST bytes (UTF-8, BOM tolerated).
/// @param level    Receives the parsed level, or ExecutionLevel::None.
[[nodiscard]] std::error_code ReadRequestedExecutionLevel(const std::vector<uint8_t>& manifest, ExecutionLevel& level);

/// @brief Read the requested execution level of a PE file's application manifest.
/// @param binary PE file to read RT_MANIFEST / CREATEPROCESS_MANIFEST_RESOURCE_ID from.
/// @param level  Receives the parsed level, or ExecutionLevel::None when the file carries no manifest.
[[nodiscard]] std::error_code
ReadRequestedExecutionLevel(const std::filesystem::path& binary, ExecutionLevel& level);

/// @brief Rewrite the 'level' attribute of a manifest blob's <requestedExecutionLevel> element in place.
///
/// Only that attribute value is substituted: everything else in the manifest (assembly identity,
/// compatibility supportedOS list, uiAccess) is left byte for byte as it was. uiAccess is deliberately
/// never written, since uiAccess="true" requires an Authenticode signed image in a secure location.
///
/// @param manifest Raw RT_MANIFEST bytes, updated on success. The blob may grow or shrink; RT_MANIFEST
///                 carries no length prefix, so the resource writer handles the new size.
/// @param level    Level to write. ExecutionLevel::None is rejected as invalid_argument.
/// @return errc::no_such_file_or_directory when there is no single unambiguous 'level' attribute to
///         rewrite, leaving @p manifest untouched.
[[nodiscard]] std::error_code SetRequestedExecutionLevel(std::vector<uint8_t>& manifest, ExecutionLevel level);
