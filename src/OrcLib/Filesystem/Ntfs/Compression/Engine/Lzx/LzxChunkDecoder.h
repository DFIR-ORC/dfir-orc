//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <array>
#include <cstdint>
#include <system_error>

#include <gsl/span>

#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxSlots.h"
#include "Utils/BufferView.h"

namespace Orc::Ntfs::Lzx {

// Decodes one WOF LZX chunk. Every call starts clean (trees, recent offsets, history), since WOF chunks are
// independent. The two code length arrays are members rather than locals only to keep them off the stack of a
// stream read; they are zeroed at every call and nothing in them outlives it.
class ChunkDecoder
{
public:
    // Returns the number of bytes produced, which is 'output.size()' on success and 0 with 'ec' set otherwise.
    [[nodiscard]] size_t Decode(BufferView input, gsl::span<uint8_t> output, std::error_code& ec);

private:
    std::array<uint8_t, kMainTreeSymbolCount> m_mainLengths {};
    std::array<uint8_t, kLengthTreeSymbolCount> m_lengthLengths {};
};

}  // namespace Orc::Ntfs::Lzx
