//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "LzxChunkDecoder.h"

#include <algorithm>

#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxBitStream.h"
#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxCodeLengths.h"
#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxE8Translation.h"
#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxHuffmanTable.h"
#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxWindow.h"
#include "Log/Log.h"
#include "Text/Fmt/std_error_code.h"

namespace {

using namespace Orc;
using namespace Orc::Ntfs::Lzx;

using RecentOffsets = std::array<uint32_t, kRecentOffsetCount>;

constexpr size_t kLiteralCount = 256;
constexpr uint32_t kLengthFooterCount = 8;

struct LzxBlockHeader
{
    uint32_t type = 0;
    uint32_t size = 0;
};

// The trees of one compressed block. The length tree alone may legitimately be empty: a block whose matches are
// all shorter than 9 never consults it, so its code lengths are all zero and no complete code exists to build.
// 'lengthTreeEmpty' records that, and a block that then asks for a long match is refused.
struct BlockTrees
{
    HuffmanTable main;
    HuffmanTable length;
    HuffmanTable aligned;
    bool lengthTreeEmpty = false;
};

uint32_t ReadLittleEndian32(BufferView bytes)
{
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8)
        | (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

// 3 bits of type, then 1 flag bit: set means the default 32768 byte block with no size field, clear means a 16 bit
// size follows. Established against the fixture: the full chunks take the flag, and only the short last chunk
// carries a size. A 24 bit size belongs to LZX dialects with a window order of 16 or more; WOF's is 15.
bool ReadBlockHeader(BitStream& stream, LzxBlockHeader& header, std::error_code& ec)
{
    header.type = stream.ReadBits(kBlockTypeBits, ec);
    const uint32_t useDefaultSize = stream.ReadBits(1, ec);
    if (ec)
    {
        return false;
    }

    if (useDefaultSize != 0)
    {
        header.size = kDefaultBlockSize;
        return true;
    }

    header.size = stream.ReadBits(kBlockSizeBits, ec);
    return !ec;
}

bool ReadAlignedTree(BitStream& stream, HuffmanTable& aligned, std::error_code& ec)
{
    std::array<uint8_t, kAlignedTreeSymbolCount> lengths {};
    for (uint8_t& length : lengths)
    {
        length = static_cast<uint8_t>(stream.ReadBits(kAlignedCodeLengthBits, ec));
        if (ec)
        {
            return false;
        }
    }

    return aligned.Build(lengths, ec);
}

bool IsAllZero(BufferView lengths)
{
    return std::all_of(lengths.begin(), lengths.end(), [](uint8_t length) { return length == 0; });
}

// The format's order: aligned tree (aligned blocks only), main tree in two halves, length tree. The length arrays
// still hold the previous block's values, which is what 'ReadCodeLengths' deltas against.
bool BuildTrees(
    BitStream& stream,
    bool isAligned,
    gsl::span<uint8_t> mainLengths,
    gsl::span<uint8_t> lengthLengths,
    BlockTrees& trees,
    std::error_code& ec)
{
    if (isAligned && !ReadAlignedTree(stream, trees.aligned, ec))
    {
        return false;
    }

    if (!ReadCodeLengths(stream, mainLengths.subspan(0, kLiteralCount), ec)
        || !ReadCodeLengths(stream, mainLengths.subspan(kLiteralCount), ec) || !trees.main.Build(mainLengths, ec))
    {
        return false;
    }

    if (!ReadCodeLengths(stream, lengthLengths, ec))
    {
        return false;
    }

    trees.lengthTreeEmpty = IsAllZero(lengthLengths);
    if (trees.lengthTreeEmpty)
    {
        return true;
    }

    return trees.length.Build(lengthLengths, ec);
}

// Slots 0, 1 and 2 name a recent offset (1 and 2 swap with 0; 0 leaves them as they are). The others carry a base
// plus extra bits, whose low three come from the aligned tree in an aligned block, and shift the recent offsets.
bool DecodeMatchOffset(
    BitStream& stream,
    size_t slot,
    bool isAligned,
    const BlockTrees& trees,
    RecentOffsets& recent,
    uint32_t& offset,
    std::error_code& ec)
{
    if (slot < kRecentOffsetCount)
    {
        offset = recent[slot];
        if (slot != 0)
        {
            std::swap(recent[0], recent[slot]);
        }

        return true;
    }

    const uint8_t extra = PositionSlotExtraBits(slot);
    uint32_t bits = 0;
    if (isAligned && extra >= kAlignedCodeLengthBits)
    {
        bits = stream.ReadBits(static_cast<uint8_t>(extra - kAlignedCodeLengthBits), ec) << kAlignedCodeLengthBits;
        bits += trees.aligned.Decode(stream, ec);
    }
    else
    {
        bits = stream.ReadBits(extra, ec);
    }

    if (ec)
    {
        return false;
    }

    // Slot 3 and up have a base of at least 3, so the adjustment cannot wrap.
    offset = PositionSlotBase(slot) + bits - kOffsetAdjustment;
    recent[2] = recent[1];
    recent[1] = recent[0];
    recent[0] = offset;
    return true;
}

// Decodes 'blockSize' bytes of literals and matches. 'Window' bounds every write against the output; the block end
// is checked here, since a match may not run across a block boundary.
bool DecodeCompressedBlock(
    BitStream& stream,
    Window& window,
    bool isAligned,
    uint32_t blockSize,
    gsl::span<uint8_t> mainLengths,
    gsl::span<uint8_t> lengthLengths,
    RecentOffsets& recent,
    std::error_code& ec)
{
    BlockTrees trees;
    if (!BuildTrees(stream, isAligned, mainLengths, lengthLengths, trees, ec))
    {
        return false;
    }

    const size_t blockEnd = window.Produced() + blockSize;
    while (window.Produced() < blockEnd)
    {
        const uint32_t symbol = trees.main.Decode(stream, ec);
        if (ec)
        {
            return false;
        }

        if (symbol < kLiteralCount)
        {
            if (!window.WriteLiteral(static_cast<uint8_t>(symbol), ec))
            {
                return false;
            }

            continue;
        }

        const uint32_t matchSymbol = symbol - kLiteralCount;
        const size_t slot = matchSymbol / kLengthFooterCount;
        const uint32_t lengthFooter = matchSymbol % kLengthFooterCount;
        if (slot >= kPositionSlotCount)
        {
            ec = std::make_error_code(std::errc::bad_message);
            Log::Debug("Lzx: position slot {} out of range [{}]", slot, ec);
            return false;
        }

        uint32_t length = lengthFooter + kMinMatchLength;
        if (lengthFooter == kPrimaryLengthCount)
        {
            if (trees.lengthTreeEmpty)
            {
                ec = std::make_error_code(std::errc::bad_message);
                Log::Debug("Lzx: a long match in a block whose length tree is empty [{}]", ec);
                return false;
            }

            length += trees.length.Decode(stream, ec);
            if (ec)
            {
                return false;
            }
        }

        if (length > blockEnd - window.Produced())
        {
            ec = std::make_error_code(std::errc::bad_message);
            Log::Debug(
                "Lzx: match of {} past the {} bytes left in the block [{}]", length, blockEnd - window.Produced(), ec);
            return false;
        }

        uint32_t offset = 0;
        if (!DecodeMatchOffset(stream, slot, isAligned, trees, recent, offset, ec)
            || !window.WriteMatch(offset, length, ec))
        {
            return false;
        }
    }

    return true;
}

// Aligns to a word (dropping 1 to 16 zero bits), reads three little endian recent offsets, then raw bytes. An odd
// size leaves one pad byte before the next block header, but when the block ends the chunk there is no next header
// and the pad byte may simply be absent, so it is only consumed while output remains. Every payload byte goes
// through 'Window::WriteLiteral', so the output bound stays enforced in one place.
bool DecodeUncompressedBlock(
    BitStream& stream,
    Window& window,
    RecentOffsets& recent,
    uint32_t blockSize,
    std::error_code& ec)
{
    stream.AlignToWord(ec);
    if (ec)
    {
        return false;
    }

    const BufferView header = stream.ReadBytes(kRecentOffsetCount * sizeof(uint32_t), ec);
    if (ec)
    {
        Log::Debug("Lzx: uncompressed block header past the input [{}]", ec);
        return false;
    }

    for (size_t i = 0; i < kRecentOffsetCount; ++i)
    {
        const uint32_t offset = ReadLittleEndian32(header.subspan(i * sizeof(uint32_t), sizeof(uint32_t)));

        // These three arrive verbatim rather than through a position slot, so they are the one place a forged
        // value reaches the match decoder unchecked. A recent offset is a match distance, so it has to name a
        // byte the window can still hold: one at least, the window size at most. 'Window::WriteMatch' re-checks
        // it against what the chunk has actually produced before any copy, so this only rejects the absurd.
        if (offset == 0 || offset > kWindowSize)
        {
            ec = std::make_error_code(std::errc::bad_message);
            Log::Debug("Lzx: recent offset {} of an uncompressed block is {} [{}]", i, offset, ec);
            return false;
        }

        recent[i] = offset;
    }

    const BufferView payload = stream.ReadBytes(blockSize, ec);
    if (ec)
    {
        Log::Debug("Lzx: uncompressed block payload of {} past the input [{}]", blockSize, ec);
        return false;
    }

    for (const uint8_t byte : payload)
    {
        if (!window.WriteLiteral(byte, ec))
        {
            return false;
        }
    }

    const bool padByteFollows = (blockSize % sizeof(uint16_t)) != 0 && window.Remaining() != 0;
    if (padByteFollows)
    {
        static_cast<void>(stream.ReadBytes(1, ec));
        if (ec)
        {
            return false;
        }
    }

    return true;
}

}  // namespace

namespace Orc::Ntfs::Lzx {

size_t ChunkDecoder::Decode(BufferView input, gsl::span<uint8_t> output, std::error_code& ec)
{
    // Every chunk is independent: trees, recent offsets and window start clean. This is what makes seeking in
    // 'WofStreamConcept' correct, and why a match can never reach before the start of the output.
    m_mainLengths.fill(0);
    m_lengthLengths.fill(0);
    RecentOffsets recent {1, 1, 1};

    BitStream stream(input);
    Window window(output);

    while (window.Remaining() != 0)
    {
        LzxBlockHeader header;
        if (!ReadBlockHeader(stream, header, ec))
        {
            return 0;
        }

        if (header.size == 0)
        {
            ec = std::make_error_code(std::errc::bad_message);
            Log::Debug("Lzx: block type {} declares no bytes [{}]", header.type, ec);
            return 0;
        }

        // Clamped, never iterated on as declared. A block announcing more than the chunk has left cannot be
        // trusted to size a loop; the window bounds every write regardless, so nothing can overrun either way.
        const uint32_t blockSize = static_cast<uint32_t>(std::min<size_t>(header.size, window.Remaining()));

        bool decoded = false;
        switch (header.type)
        {
            case kBlockVerbatim:
            case kBlockAligned:
                decoded = DecodeCompressedBlock(
                    stream,
                    window,
                    header.type == kBlockAligned,
                    blockSize,
                    m_mainLengths,
                    m_lengthLengths,
                    recent,
                    ec);
                break;
            case kBlockUncompressed:
                decoded = DecodeUncompressedBlock(stream, window, recent, blockSize, ec);
                break;
            default:
                ec = std::make_error_code(std::errc::bad_message);
                Log::Debug("Lzx: block type {} is not 1, 2 or 3 [{}]", header.type, ec);
                return 0;
        }

        if (!decoded)
        {
            return 0;
        }
    }

    // The WIM dialect's encoder always ran x86 call translation; undo it now that the chunk is whole.
    UndoE8Translation(output);
    return window.Produced();
}

}  // namespace Orc::Ntfs::Lzx
