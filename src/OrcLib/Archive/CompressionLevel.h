//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
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

CompressionLevel ToCompressionLevel(const std::wstring& compressionLevel, std::error_code& ec);

}  // namespace Archive
}  // namespace Orc
