//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

[[nodiscard]] std::error_code ComputeSha256(const std::filesystem::path& path, std::wstring& sha256);
[[nodiscard]] std::error_code ComputeSha256(const std::filesystem::path& path, std::vector<uint8_t>& sha256);
[[nodiscard]] std::error_code ComputeSha256(const std::vector<uint8_t>& data, std::wstring& sha256);
[[nodiscard]] std::error_code ComputeSha256(const std::vector<uint8_t>& data, std::vector<uint8_t>& sha256);
