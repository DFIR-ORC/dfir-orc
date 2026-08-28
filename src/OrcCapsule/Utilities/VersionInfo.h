//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

/// @brief Append a marker to the FileDescription of a raw RT_VERSION (VS_VERSIONINFO) resource.
///
/// When the capsule adopts an embedded binary's version resource it loses every DFIR-ORC identity string.
/// To leave a small, deliberate trace, this appends @p marker (e.g. L" (capsule)") to the FileDescription
/// value of the resource's primary string table. If no FileDescription exists, one is inserted with the
/// marker (leading space trimmed) as its value so the marker always lands.
///
/// The resource is parsed and re-serialized in place. On any parse failure @p versionResource is left
/// unchanged and an error is returned; callers may treat the marker as best-effort.
///
/// @param versionResource Raw bytes of the RT_VERSION resource; rewritten in place on success.
/// @param marker          Text to append to FileDescription (e.g. L" (capsule)").
[[nodiscard]] std::error_code
AppendFileDescriptionMarker(std::vector<uint8_t>& versionResource, const std::wstring& marker);
