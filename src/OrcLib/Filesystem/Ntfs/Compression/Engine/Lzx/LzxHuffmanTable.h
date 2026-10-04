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

#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxBitStream.h"
#include "Utils/BufferView.h"

namespace Orc::Ntfs::Lzx {

// Canonical Huffman decoding driven by the count of codes per length and the first symbol index of each length.
// A flat lookup table would need 1 << 16 entries per tree for LZX's maximum code length, so the slower walk is
// kept for clarity; if a profile ever justifies it, add a small first level table in front of this loop.
class HuffmanTable
{
public:
    static constexpr uint8_t kMaxCodeLength = 16;
    static constexpr size_t kMaxSymbols = 496;  // the main tree for a 32KB window

    // 'lengths[symbol]' is the code length of 'symbol', 0 for an absent symbol. Refuses an incomplete or over
    // subscribed code, and an all zero one: the caller decides whether an empty tree is legal where it stands.
    [[nodiscard]] bool Build(BufferView lengths, std::error_code& ec);
    [[nodiscard]] uint32_t Decode(BitStream& stream, std::error_code& ec) const;

private:
    std::array<uint16_t, kMaxCodeLength + 1> m_countPerLength {};
    std::array<uint32_t, kMaxCodeLength + 1> m_firstCodePerLength {};
    std::array<uint16_t, kMaxCodeLength + 1> m_firstIndexPerLength {};
    std::array<uint16_t, kMaxSymbols> m_symbolsByCode {};
    size_t m_symbolCount = 0;
};

}  // namespace Orc::Ntfs::Lzx
