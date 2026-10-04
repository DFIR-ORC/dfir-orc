//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <cstdint>
#include <system_error>

#include <gsl/span>

#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxBitStream.h"

namespace Orc::Ntfs::Lzx {

constexpr size_t kPretreeSymbolCount = 20;

// Reads one code length per entry of 'lengths', delta coded against the values already there, through a 20 symbol
// pretree of 4 bit lengths. Returns false and sets 'ec' on malformed input, leaving 'lengths' partly updated: the
// caller must discard the whole tree, never use it.
[[nodiscard]] bool ReadCodeLengths(BitStream& stream, gsl::span<uint8_t> lengths, std::error_code& ec);

}  // namespace Orc::Ntfs::Lzx
