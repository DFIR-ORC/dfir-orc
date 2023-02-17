//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace rcedit {

void Compress7z(
    const std::filesystem::path& inputFile,
    std::vector< uint8_t >& out,
    std::error_code& ec );

void Compress7z(
    std::string_view content,
    const std::wstring& fileNameInArchive,
    std::vector< uint8_t >& out,
    std::error_code& ec );

void Compress7z(
    const std::vector< uint8_t >& content,
    const std::wstring& fileNameInArchive,
    std::vector< uint8_t >& out,
    std::error_code& ec );

}  // namespace rcedit
