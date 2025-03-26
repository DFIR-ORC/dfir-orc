//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string>
#include <system_error>

namespace Orc {
namespace Archive {

enum class CompressionLevel
{
    kDefault = 0,
    kNone,
    kFastest,
    kFast,
    kNormal,
    kMaximum,
    kUltra
};

CompressionLevel ToCompressionLevel(std::wstring_view compressionLevel, std::error_code& ec);

std::string_view ToString(CompressionLevel);
std::wstring_view ToWString(CompressionLevel);

}  // namespace Archive
}  // namespace Orc
