//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Orc::Ntfs::Lzx {

// The WOF flavour of LZX: a 32KB window (WofBitOrder::kLzx is 15), which fixes the slot count and the tree sizes.
constexpr size_t kWindowSize = 32768;
constexpr size_t kPositionSlotCount = 30;
constexpr size_t kMainTreeSymbolCount = 256 + 8 * kPositionSlotCount;  // 496
constexpr size_t kLengthTreeSymbolCount = 249;
constexpr size_t kAlignedTreeSymbolCount = 8;
constexpr uint32_t kMinMatchLength = 2;
constexpr uint32_t kPrimaryLengthCount = 7;
constexpr uint32_t kOffsetAdjustment = 2;
constexpr size_t kRecentOffsetCount = 3;
constexpr uint8_t kAlignedCodeLengthBits = 3;

constexpr uint32_t kBlockVerbatim = 1;
constexpr uint32_t kBlockAligned = 2;
constexpr uint32_t kBlockUncompressed = 3;

// The block header: 3 bits of type, then a flag bit standing for a whole 32768 byte block, else a 16 bit size.
// A 24 bit size belongs to LZX dialects whose window order is 16 or more, so it has no constant here on purpose.
constexpr uint8_t kBlockTypeBits = 3;
constexpr uint8_t kBlockSizeBits = 16;
constexpr uint32_t kDefaultBlockSize = 32768;

// Position slot tables. The extra bits are 0 for the first four slots and then grow by one every two slots, which
// is what makes the bases double. Both are written out rather than computed, so the assertion below checks data
// against the window size instead of checking a formula against itself.
inline constexpr std::array<uint8_t, kPositionSlotCount> kSlotExtraBits = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

inline constexpr std::array<uint32_t, kPositionSlotCount> kSlotBases = {
    0,   1,   2,   3,   4,   6,    8,    12,   16,   24,   32,   48,   64,    96,    128,
    192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096, 6144, 8192, 12288, 16384, 24576};

// A slot out of range yields 0, so a caller that forgot to range check cannot read a stray table entry.
[[nodiscard]] constexpr uint8_t PositionSlotExtraBits(size_t slot) noexcept
{
    return slot < kSlotExtraBits.size() ? kSlotExtraBits[slot] : uint8_t {0};
}

[[nodiscard]] constexpr uint32_t PositionSlotBase(size_t slot) noexcept
{
    return slot < kSlotBases.size() ? kSlotBases[slot] : uint32_t {0};
}

namespace Detail {

// The slots partition the window exactly: every base is the previous one plus what its extra bits can address, and
// the slot past the last one would start at the window size. An off by one in either table breaks this, and a
// wrong table decodes a match to the wrong offset, which is silent corruption rather than a failure.
[[nodiscard]] constexpr bool PositionSlotsCoverTheWindow() noexcept
{
    uint32_t base = 0;
    for (size_t slot = 0; slot < kPositionSlotCount; ++slot)
    {
        if (PositionSlotBase(slot) != base)
        {
            return false;
        }

        base += uint32_t {1} << PositionSlotExtraBits(slot);
    }

    return base == kWindowSize;
}

}  // namespace Detail

static_assert(Detail::PositionSlotsCoverTheWindow(), "the position slot tables must partition the window exactly");

}  // namespace Orc::Ntfs::Lzx
