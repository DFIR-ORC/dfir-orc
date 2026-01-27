//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <system_error>
#include <vector>

[[nodiscard]] bool Is7zArchive(std::basic_string_view<uint8_t> data);
[[nodiscard]] bool IsZstdArchive(std::basic_string_view<uint8_t> data);

[[nodiscard]] std::error_code ZstdCompressData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);
[[nodiscard]] std::error_code ZstdDecompressData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);
[[nodiscard]] std::error_code
ZstdGetCompressRatio(std::basic_string_view<uint8_t> buffer, uint64_t& compressedSize, double& ratio);
