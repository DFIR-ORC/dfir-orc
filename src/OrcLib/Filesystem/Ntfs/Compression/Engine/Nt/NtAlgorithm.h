//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string>
#include <system_error>

namespace Orc {

enum class NtAlgorithm
{
    kUnknown = -1,
    kNone = 0,  // COMPRESSION_FORMAT_NONE
    kDefault = 1,  // COMPRESSION_FORMAT_DEFAULT
    kLznt1 = 2,  // COMPRESSION_FORMAT_LZNT1
    kXpress = 3,  // COMPRESSION_FORMAT_XPRESS
    kXpressHuffman = 4  // COMPRESSION_FORMAT_XPRESS_HUFF (>= Windows 8)
};

std::string_view ToString(NtAlgorithm algorithm);
NtAlgorithm ToNtAlgorithm(const std::string& algorithm, std::error_code& ec);

}  // namespace Orc
