//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "stdafx.h"

#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxBitStream.h"
#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxChunkDecoder.h"
#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxCodeLengths.h"
#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxDecompressorConcept.h"
#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxE8Translation.h"
#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxHuffmanTable.h"
#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxSlots.h"
#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxWindow.h"

#include <array>
#include <fstream>
#include <iterator>
#include <random>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Ntfs;

namespace Orc::Test {

// Reads a file under tests/OrcLibTest. Empty on failure; callers assert on the expected size.
std::vector<uint8_t> LzxReadFixture(std::wstring_view relative)
{
    const std::wstring path = UnitTestHelper().GetDirectoryName(__WFILE__) + L"\\" + std::wstring(relative);
    std::ifstream file(path, std::ios::binary);
    if (!file.good())
    {
        return {};
    }

    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

uint32_t LzxTestReadLittleEndian32(const std::vector<uint8_t>& blob, size_t at)
{
    return static_cast<uint32_t>(blob[at]) | (static_cast<uint32_t>(blob[at + 1]) << 8)
        | (static_cast<uint32_t>(blob[at + 2]) << 16) | (static_cast<uint32_t>(blob[at + 3]) << 24);
}

// Slices chunk 'index' out of a WOF stream of 'chunkCount' chunks whose offset table holds 'chunkCount - 1'
// 32 bit entries. Mirrors 'WofChunks::ParseLocations' deliberately: if the two ever disagree, the tests say so.
std::vector<uint8_t> LzxTestChunk(const std::vector<uint8_t>& blob, size_t chunkCount, size_t index)
{
    const size_t tableSize = (chunkCount - 1) * sizeof(uint32_t);

    size_t begin = tableSize;
    if (index != 0)
    {
        begin = tableSize + LzxTestReadLittleEndian32(blob, (index - 1) * sizeof(uint32_t));
    }

    size_t end = blob.size();
    if (index + 1 < chunkCount)
    {
        end = tableSize + LzxTestReadLittleEndian32(blob, index * sizeof(uint32_t));
    }

    return std::vector<uint8_t>(blob.begin() + begin, blob.begin() + end);
}

std::vector<uint8_t> LzxReadKnownPlaintextBlob()
{
    const std::vector<uint8_t> blob = LzxReadFixture(L"binaries\\wof_lzx_known_plaintext.wofdata");
    Assert::AreEqual(size_t(2764), blob.size(), L"fixture wof_lzx_known_plaintext.wofdata is missing");
    return blob;
}

// Writes bits most significant first into 16 bit little endian words, the order 'Lzx::BitStream' reads.
class LzxTestBitWriter
{
public:
    void WriteBits(uint32_t value, uint8_t count)
    {
        for (uint8_t i = 0; i < count; ++i)
        {
            const uint32_t bit = (value >> (count - 1 - i)) & 1;
            m_word = static_cast<uint16_t>((m_word << 1) | bit);
            if (++m_bits == 16)
            {
                Flush();
            }
        }
    }

    std::vector<uint8_t> Finish()
    {
        while (m_bits != 0)
        {
            WriteBits(0, 1);
        }

        return m_bytes;
    }

private:
    void Flush()
    {
        m_bytes.push_back(static_cast<uint8_t>(m_word & 0xFF));
        m_bytes.push_back(static_cast<uint8_t>(m_word >> 8));
        m_word = 0;
        m_bits = 0;
    }

    std::vector<uint8_t> m_bytes;
    uint16_t m_word = 0;
    uint8_t m_bits = 0;
};

TEST_CLASS(LzxBitStreamTest)
{
public:
    // LZX reads 16 bit little endian words and consumes bits from the most significant bit of each word.
    // 0x1234 stored little endian is 34 12, and its first four bits are 0x1.
    TEST_METHOD(BitsComeFromTheTopOfEachLittleEndianWord)
    {
        const std::array<uint8_t, 4> bytes {0x34, 0x12, 0x78, 0x56};
        Lzx::BitStream stream(BufferView(bytes.data(), bytes.size()));

        std::error_code ec;
        Assert::AreEqual(0x1u, stream.ReadBits(4, ec));
        Assert::AreEqual(0x2u, stream.ReadBits(4, ec));
        Assert::AreEqual(0x34u, stream.ReadBits(8, ec));
        Assert::AreEqual(0x5678u, stream.ReadBits(16, ec));
        Assert::IsFalse(static_cast<bool>(ec));
        Assert::IsTrue(stream.Exhausted());
    }

    // A field may straddle a word boundary: words 0xF0FF and 0x000F read as 4, 16 and 12 bits.
    TEST_METHOD(AFieldCanStraddleTwoWords)
    {
        const std::array<uint8_t, 4> bytes {0xFF, 0xF0, 0x0F, 0x00};
        Lzx::BitStream stream(BufferView(bytes.data(), bytes.size()));

        std::error_code ec;
        Assert::AreEqual(0xFu, stream.ReadBits(4, ec));
        Assert::AreEqual(0x0FF0u, stream.ReadBits(16, ec));
        Assert::AreEqual(0x00Fu, stream.ReadBits(12, ec));
        Assert::IsFalse(static_cast<bool>(ec));
        Assert::IsTrue(stream.Exhausted());
    }

    // Before an uncompressed block the stream drops 1 to 16 bits, never 0, so on a word boundary a whole word goes.
    TEST_METHOD(AlignOnAWordBoundarySkipsAWholeWord)
    {
        const std::array<uint8_t, 6> bytes {0x34, 0x12, 0x00, 0x00, 0xAA, 0xBB};
        Lzx::BitStream stream(BufferView(bytes.data(), bytes.size()));

        std::error_code ec;
        Assert::AreEqual(0x1234u, stream.ReadBits(16, ec));
        stream.AlignToWord(ec);
        Assert::IsFalse(static_cast<bool>(ec));

        const BufferView raw = stream.ReadBytes(2, ec);
        Assert::IsFalse(static_cast<bool>(ec));
        Assert::AreEqual(0xAA, static_cast<int>(raw[0]));
        Assert::AreEqual(0xBB, static_cast<int>(raw[1]));
    }

    // The dropped bits are zero padding; anything else means a forged or desynchronised stream.
    TEST_METHOD(NonZeroPaddingBitsAreRefused)
    {
        const std::array<uint8_t, 4> bytes {0x34, 0x12, 0x01, 0x02};
        Lzx::BitStream stream(BufferView(bytes.data(), bytes.size()));

        std::error_code ec;
        Assert::AreEqual(0x1u, stream.ReadBits(4, ec));
        stream.AlignToWord(ec);
        Assert::IsTrue(static_cast<bool>(ec));
    }

    // Reading past the end sets 'ec' and returns 0 instead of reading the bytes that follow the buffer.
    TEST_METHOD(ReadingPastTheEndFailsClosed)
    {
        const std::array<uint8_t, 2> bytes {0xAA, 0xBB};
        Lzx::BitStream stream(BufferView(bytes.data(), bytes.size()));

        std::error_code ec;
        Assert::AreEqual(0xBBAAu, stream.ReadBits(16, ec));
        Assert::IsFalse(static_cast<bool>(ec));

        Assert::AreEqual(0u, stream.ReadBits(1, ec));
        Assert::IsTrue(static_cast<bool>(ec));
    }

    // An odd sized buffer cannot hold a whole word: the stream refuses rather than reading one byte past.
    TEST_METHOD(AnOddSizedBufferNeverYieldsAPartialWord)
    {
        const std::array<uint8_t, 3> bytes {0x34, 0x12, 0x99};
        Lzx::BitStream stream(BufferView(bytes.data(), bytes.size()));

        std::error_code ec;
        Assert::AreEqual(0x1234u, stream.ReadBits(16, ec));
        Assert::IsFalse(static_cast<bool>(ec));

        Assert::AreEqual(0u, stream.ReadBits(1, ec));
        Assert::IsTrue(static_cast<bool>(ec));
    }

    // 'AlignToWord' drops the zero padding left in the current word; 'ReadBytes' then returns raw bytes from there.
    TEST_METHOD(AlignThenReadRawBytes)
    {
        const std::array<uint8_t, 6> bytes {0x00, 0x10, 0x01, 0x02, 0x03, 0x04};
        Lzx::BitStream stream(BufferView(bytes.data(), bytes.size()));

        std::error_code ec;
        Assert::AreEqual(0x1u, stream.ReadBits(4, ec));
        stream.AlignToWord(ec);
        Assert::IsFalse(static_cast<bool>(ec));

        const BufferView raw = stream.ReadBytes(4, ec);
        Assert::IsFalse(static_cast<bool>(ec));
        Assert::AreEqual(size_t(4), raw.size());
        Assert::AreEqual(1, static_cast<int>(raw[0]));
        Assert::AreEqual(4, static_cast<int>(raw[3]));

        static_cast<void>(stream.ReadBytes(1, ec));
        Assert::IsTrue(static_cast<bool>(ec));
    }
};

TEST_CLASS(LzxChunkEntryTest)
{
public:
    // A WOF LZX chunk opens directly on a block header, with no x86 translation header bit. The header is 3 bits
    // of type in 1..3, then 1 flag bit: set means a 32768 byte block and no size field, clear means a 16 bit size
    // follows. Chunk 0 decodes to a full 32768 bytes, so either the flag is set or a size of at most 32768 follows.
    // Observed on the fixture (compact /c /exe:lzx, Windows 11 26200): type 2 (aligned offset), flag 1.
    TEST_METHOD(AFullChunkOpensOnAWimBlockHeader)
    {
        const std::vector<uint8_t> chunk = LzxTestChunk(LzxReadKnownPlaintextBlob(), 7, 0);
        Assert::AreEqual(size_t(400), chunk.size());

        std::error_code ec;
        Lzx::BitStream stream(BufferView(chunk.data(), chunk.size()));

        const uint32_t blockType = stream.ReadBits(3, ec);
        const uint32_t useDefaultSize = stream.ReadBits(1, ec);
        Assert::IsFalse(static_cast<bool>(ec));
        Assert::IsTrue(blockType >= 1 && blockType <= 3, L"block type out of 1..3");

        if (useDefaultSize == 0)
        {
            const uint32_t blockSize = stream.ReadBits(16, ec);
            Assert::IsFalse(static_cast<bool>(ec));
            Assert::IsTrue(blockSize > 0 && blockSize <= 32768, L"block size outside one chunk");
        }
    }

    // The last chunk holds 3392 bytes, so its first block cannot be the 32768 byte default: the flag must be clear
    // and the 16 bit size at most 3392 (a window order of 15 adds no extra 8 bits).
    // Observed on the fixture: type 1 (verbatim), flag 0, size 3392: one block covers the whole chunk.
    TEST_METHOD(TheShortLastChunkCarriesASixteenBitSize)
    {
        const std::vector<uint8_t> chunk = LzxTestChunk(LzxReadKnownPlaintextBlob(), 7, 6);
        Assert::AreEqual(size_t(338), chunk.size());

        std::error_code ec;
        Lzx::BitStream stream(BufferView(chunk.data(), chunk.size()));

        const uint32_t blockType = stream.ReadBits(3, ec);
        const uint32_t useDefaultSize = stream.ReadBits(1, ec);
        const uint32_t blockSize = stream.ReadBits(16, ec);
        Assert::IsFalse(static_cast<bool>(ec));
        Assert::IsTrue(blockType >= 1 && blockType <= 3, L"block type out of 1..3");
        Assert::AreEqual(0u, useDefaultSize, L"a 3392 byte chunk cannot open on a 32768 byte block");
        Assert::IsTrue(blockSize > 0 && blockSize <= 3392, L"block size outside the last chunk");
    }
};

TEST_CLASS(LzxHuffmanTableTest)
{
public:
    // Canonical codes: lengths {1,2,3,3} over symbols 0..3 give codes 0, 10, 110, 111.
    TEST_METHOD(DecodesCanonicalCodes)
    {
        const std::array<uint8_t, 4> lengths {1, 2, 3, 3};
        Lzx::HuffmanTable table;
        std::error_code ec;
        Assert::IsTrue(table.Build(BufferView(lengths.data(), lengths.size()), ec));
        Assert::IsFalse(static_cast<bool>(ec));

        // bits: 0 10 110 111 then 7 zero bits -> 0101 1011 1000 0000 = 0x5B80 -> little endian 80 5B
        const std::array<uint8_t, 2> bytes {0x80, 0x5B};
        Lzx::BitStream stream(BufferView(bytes.data(), bytes.size()));

        Assert::AreEqual(0u, table.Decode(stream, ec));
        Assert::AreEqual(1u, table.Decode(stream, ec));
        Assert::AreEqual(2u, table.Decode(stream, ec));
        Assert::AreEqual(3u, table.Decode(stream, ec));
        Assert::IsFalse(static_cast<bool>(ec));
    }

    // A symbol with length 0 is absent from the code and must never be produced.
    TEST_METHOD(ZeroLengthSymbolsAreAbsent)
    {
        const std::array<uint8_t, 4> lengths {0, 1, 0, 1};
        Lzx::HuffmanTable table;
        std::error_code ec;
        Assert::IsTrue(table.Build(BufferView(lengths.data(), lengths.size()), ec));

        const std::array<uint8_t, 2> bytes {0x00, 0x40};  // bits 0 1 ...
        Lzx::BitStream stream(BufferView(bytes.data(), bytes.size()));
        Assert::AreEqual(1u, table.Decode(stream, ec));
        Assert::AreEqual(3u, table.Decode(stream, ec));
        Assert::IsFalse(static_cast<bool>(ec));
    }

    // More codes than the length allows: the set is over subscribed and the table is refused.
    TEST_METHOD(OverSubscribedLengthsAreRefused)
    {
        const std::array<uint8_t, 3> lengths {1, 1, 1};
        Lzx::HuffmanTable table;
        std::error_code ec;
        Assert::IsFalse(table.Build(BufferView(lengths.data(), lengths.size()), ec));
        Assert::IsTrue(static_cast<bool>(ec));
    }

    // Fewer codes than the length allows leaves holes a forged stream could fall into: refused too.
    TEST_METHOD(IncompleteLengthsAreRefused)
    {
        const std::array<uint8_t, 3> lengths {1, 2, 0};
        Lzx::HuffmanTable table;
        std::error_code ec;
        Assert::IsFalse(table.Build(BufferView(lengths.data(), lengths.size()), ec));
        Assert::IsTrue(static_cast<bool>(ec));
    }

    // All zero lengths is no code at all. The table refuses it; the one tree allowed to be empty (the length
    // tree) is handled by the chunk decoder, which then never consults it.
    TEST_METHOD(AllZeroLengthsIsRefused)
    {
        const std::array<uint8_t, 3> lengths {0, 0, 0};
        Lzx::HuffmanTable table;
        std::error_code ec;
        Assert::IsFalse(table.Build(BufferView(lengths.data(), lengths.size()), ec));
        Assert::IsTrue(static_cast<bool>(ec));
    }

    // Decoding from a stream that runs out mid code sets 'ec' rather than returning a symbol.
    TEST_METHOD(DecodeFromAnExhaustedStreamFailsClosed)
    {
        const std::array<uint8_t, 4> lengths {1, 2, 3, 3};
        Lzx::HuffmanTable table;
        std::error_code ec;
        Assert::IsTrue(table.Build(BufferView(lengths.data(), lengths.size()), ec));

        Lzx::BitStream stream(BufferView {});
        static_cast<void>(table.Decode(stream, ec));
        Assert::IsTrue(static_cast<bool>(ec));
    }
};

TEST_CLASS(LzxCodeLengthsTest)
{
public:
    // The pretree is 20 symbols of 4 bit lengths, then each output length is coded against its previous value:
    // a symbol s in 0..16 sets the new length to (previous - s + 17) % 17, so s == 0 repeats the previous one.
    TEST_METHOD(ASymbolOfZeroRepeatsThePreviousLength)
    {
        // pretree: symbols 0 and 1, both length 1 (a single symbol would be an incomplete code).
        LzxTestBitWriter writer;
        for (size_t i = 0; i < 20; ++i)
        {
            writer.WriteBits(i < 2 ? 1 : 0, 4);
        }

        // four output lengths, all coded with pretree symbol 0 (code '0'), meaning "same as before"
        for (size_t i = 0; i < 4; ++i)
        {
            writer.WriteBits(0, 1);
        }

        const std::vector<uint8_t> bytes = writer.Finish();
        Lzx::BitStream stream(BufferView(bytes.data(), bytes.size()));

        std::array<uint8_t, 4> lengths {7, 7, 7, 7};
        std::error_code ec;
        Assert::IsTrue(Lzx::ReadCodeLengths(stream, gsl::make_span(lengths), ec));
        Assert::IsFalse(static_cast<bool>(ec));
        for (const uint8_t length : lengths)
        {
            Assert::AreEqual(7, static_cast<int>(length));
        }
    }

    // Symbol 17 is a run of zeros: 4 extra bits plus 4.
    TEST_METHOD(SymbolSeventeenRunsZeros)
    {
        LzxTestBitWriter writer;
        for (size_t i = 0; i < 20; ++i)
        {
            writer.WriteBits(i == 0 || i == 17 ? 1 : 0, 4);
        }

        // pretree symbols by canonical order: symbol 0 -> code '0', symbol 17 -> code '1'
        writer.WriteBits(1, 1);  // symbol 17
        writer.WriteBits(1, 4);  // run of 4 + 1 = 5 zeros
        const std::vector<uint8_t> bytes = writer.Finish();
        Lzx::BitStream stream(BufferView(bytes.data(), bytes.size()));

        std::array<uint8_t, 5> lengths {3, 3, 3, 3, 3};
        std::error_code ec;
        Assert::IsTrue(Lzx::ReadCodeLengths(stream, gsl::make_span(lengths), ec));
        for (const uint8_t length : lengths)
        {
            Assert::AreEqual(0, static_cast<int>(length));
        }
    }

    // A run that would write past the requested range is refused rather than clamped silently: the stream is
    // malformed and continuing would decode garbage with a plausible looking table.
    TEST_METHOD(ARunPastTheRangeIsRefused)
    {
        LzxTestBitWriter writer;
        for (size_t i = 0; i < 20; ++i)
        {
            writer.WriteBits(i == 0 || i == 18 ? 1 : 0, 4);
        }

        writer.WriteBits(1, 1);  // symbol 18
        writer.WriteBits(31, 5);  // run of 31 + 20 = 51 zeros, far past the 4 requested
        const std::vector<uint8_t> bytes = writer.Finish();
        Lzx::BitStream stream(BufferView(bytes.data(), bytes.size()));

        std::array<uint8_t, 4> lengths {1, 1, 1, 1};
        std::error_code ec;
        Assert::IsFalse(Lzx::ReadCodeLengths(stream, gsl::make_span(lengths), ec));
        Assert::IsTrue(static_cast<bool>(ec));
    }

    // Truncated input: the pretree itself does not fit.
    TEST_METHOD(ATruncatedPretreeIsRefused)
    {
        const std::array<uint8_t, 2> bytes {0x11, 0x11};
        Lzx::BitStream stream(BufferView(bytes.data(), bytes.size()));

        std::array<uint8_t, 4> lengths {1, 1, 1, 1};
        std::error_code ec;
        Assert::IsFalse(Lzx::ReadCodeLengths(stream, gsl::make_span(lengths), ec));
        Assert::IsTrue(static_cast<bool>(ec));
    }
};

TEST_CLASS(LzxWindowTest)
{
public:
    TEST_METHOD(LiteralsThenAnOverlappingMatch)
    {
        std::array<uint8_t, 8> output {};
        Lzx::Window window(gsl::make_span(output));

        std::error_code ec;
        Assert::IsTrue(window.WriteLiteral('a', ec));
        Assert::IsTrue(window.WriteLiteral('b', ec));

        // offset 2, length 6 overlaps itself and must repeat "ab" three times, byte by byte.
        Assert::IsTrue(window.WriteMatch(2, 6, ec));
        Assert::IsFalse(static_cast<bool>(ec));
        Assert::AreEqual(size_t(8), window.Produced());

        const std::array<uint8_t, 8> expected {'a', 'b', 'a', 'b', 'a', 'b', 'a', 'b'};
        Assert::IsTrue(std::equal(output.begin(), output.end(), expected.begin()));
    }

    // Review Focus 4: a match reaching before the start of the chunk must fail closed. Chunks are independent,
    // so there is no earlier history, and this is the obvious way to read out of bounds.
    TEST_METHOD(AMatchBeforeTheStartOfTheChunkFailsClosed)
    {
        std::array<uint8_t, 8> output {};
        Lzx::Window window(gsl::make_span(output));

        std::error_code ec;
        Assert::IsTrue(window.WriteLiteral('a', ec));

        Assert::IsFalse(window.WriteMatch(2, 1, ec));
        Assert::IsTrue(static_cast<bool>(ec));
        Assert::AreEqual(size_t(1), window.Produced());
    }

    // An offset of zero names no byte at all.
    TEST_METHOD(AZeroOffsetFailsClosed)
    {
        std::array<uint8_t, 8> output {};
        Lzx::Window window(gsl::make_span(output));

        std::error_code ec;
        Assert::IsTrue(window.WriteLiteral('a', ec));
        Assert::IsFalse(window.WriteMatch(0, 1, ec));
        Assert::IsTrue(static_cast<bool>(ec));
    }

    TEST_METHOD(AMatchPastTheOutputFailsClosed)
    {
        std::array<uint8_t, 4> output {};
        Lzx::Window window(gsl::make_span(output));

        std::error_code ec;
        Assert::IsTrue(window.WriteLiteral('a', ec));
        Assert::IsTrue(window.WriteLiteral('b', ec));

        Assert::IsFalse(window.WriteMatch(2, 99, ec));
        Assert::IsTrue(static_cast<bool>(ec));
        Assert::AreEqual(size_t(2), window.Produced());
    }

    TEST_METHOD(ALiteralPastTheOutputFailsClosed)
    {
        std::array<uint8_t, 1> output {};
        Lzx::Window window(gsl::make_span(output));

        std::error_code ec;
        Assert::IsTrue(window.WriteLiteral('a', ec));
        Assert::IsFalse(window.WriteLiteral('b', ec));
        Assert::IsTrue(static_cast<bool>(ec));
    }
};

TEST_CLASS(LzxSlotsTest)
{
public:
    // The slot bases partition the whole window exactly: base[0] is 0 and the slot after the last one starts at
    // the window size. If either table were off by one entry this would not hold.
    TEST_METHOD(SlotBasesCoverTheWindowExactly)
    {
        Assert::AreEqual(0u, Lzx::PositionSlotBase(0));

        uint32_t base = 0;
        for (size_t slot = 0; slot < Lzx::kPositionSlotCount; ++slot)
        {
            Assert::AreEqual(base, Lzx::PositionSlotBase(slot), std::to_wstring(slot).c_str());
            base += 1u << Lzx::PositionSlotExtraBits(slot);
        }

        Assert::AreEqual(static_cast<uint32_t>(Lzx::kWindowSize), base);
    }

    TEST_METHOD(TheFirstFourSlotsTakeNoExtraBits)
    {
        for (size_t slot = 0; slot < 4; ++slot)
        {
            Assert::AreEqual(0, static_cast<int>(Lzx::PositionSlotExtraBits(slot)));
        }

        Assert::AreEqual(1, static_cast<int>(Lzx::PositionSlotExtraBits(4)));
        Assert::AreEqual(1, static_cast<int>(Lzx::PositionSlotExtraBits(5)));
        Assert::AreEqual(2, static_cast<int>(Lzx::PositionSlotExtraBits(6)));
    }

    // A slot index out of range yields 0, so a caller that forgot to range check cannot read a stray table entry.
    TEST_METHOD(AnOutOfRangeSlotIsZero)
    {
        Assert::AreEqual(0, static_cast<int>(Lzx::PositionSlotExtraBits(Lzx::kPositionSlotCount)));
        Assert::AreEqual(0u, Lzx::PositionSlotBase(Lzx::kPositionSlotCount));
    }

    TEST_METHOD(TheMainTreeHasOneSymbolPerLiteralAndSlotLengthPair)
    {
        Assert::AreEqual(size_t(496), Lzx::kMainTreeSymbolCount);
    }
};

TEST_CLASS(LzxE8TranslationTest)
{
private:
    static void PutInt32(std::vector<uint8_t>& buffer, size_t at, int32_t value)
    {
        const uint32_t bits = static_cast<uint32_t>(value);
        for (size_t i = 0; i < sizeof(bits); ++i)
        {
            buffer[at + i] = static_cast<uint8_t>(bits >> (8 * i));
        }
    }

    static int32_t GetInt32(const std::vector<uint8_t>& buffer, size_t at)
    {
        return static_cast<int32_t>(LzxTestReadLittleEndian32(buffer, at));
    }

public:
    // An absolute target inside the file size becomes relative to the E8's own position.
    TEST_METHOD(AnAbsoluteTargetBecomesRelative)
    {
        std::vector<uint8_t> chunk(64, 0x90);
        chunk[20] = 0xE8;
        PutInt32(chunk, 21, 1000);

        Lzx::UndoE8Translation(gsl::make_span(chunk));
        Assert::AreEqual(1000 - 20, GetInt32(chunk, 21));
    }

    // A negative value down to minus the position is a target before the chunk start, wrapped by the file size.
    TEST_METHOD(ANegativeTargetWrapsByTheFileSize)
    {
        std::vector<uint8_t> chunk(64, 0x90);
        chunk[20] = 0xE8;
        PutInt32(chunk, 21, -20);

        Lzx::UndoE8Translation(gsl::make_span(chunk));
        Assert::AreEqual(static_cast<int32_t>(Lzx::kE8FileSize) - 20, GetInt32(chunk, 21));
    }

    // Values outside both ranges are left alone: one just below minus the position, one at the file size.
    TEST_METHOD(AnOutOfRangeValueIsUntouched)
    {
        std::vector<uint8_t> chunk(64, 0x90);
        chunk[20] = 0xE8;
        PutInt32(chunk, 21, -21);
        chunk[40] = 0xE8;
        PutInt32(chunk, 41, static_cast<int32_t>(Lzx::kE8FileSize));

        Lzx::UndoE8Translation(gsl::make_span(chunk));
        Assert::AreEqual(-21, GetInt32(chunk, 21));
        Assert::AreEqual(static_cast<int32_t>(Lzx::kE8FileSize), GetInt32(chunk, 41));
    }

    // The 4 bytes after an E8 are its operand, not opcodes: an E8 inside them is skipped. The operand 0xE814
    // translates to 0xE800 (59412 - 20), so an E8 byte sits at position 22 after the rewrite; a scan that did
    // not skip the operand would take it for a call and rewrite bytes 23 to 26, whose value 0 is translatable.
    TEST_METHOD(TheOperandOfACallIsSkipped)
    {
        std::vector<uint8_t> chunk(64, 0x90);
        chunk[20] = 0xE8;
        PutInt32(chunk, 21, 0x0000E814);
        chunk[25] = 0x00;
        chunk[26] = 0x00;

        Lzx::UndoE8Translation(gsl::make_span(chunk));
        Assert::AreEqual(0x0000E800, GetInt32(chunk, 21));
        Assert::AreEqual(0, static_cast<int>(chunk[25]));
        Assert::AreEqual(0, static_cast<int>(chunk[26]));
    }

    // The last 10 bytes of a chunk are never translated, and a chunk of 10 bytes or fewer is left whole.
    TEST_METHOD(TheTailOfAChunkIsLeftAlone)
    {
        std::vector<uint8_t> chunk(64, 0x90);
        chunk[49] = 0xE8;  // 49 < 54: inside the scan
        PutInt32(chunk, 50, 1000);
        chunk[54] = 0xE8;  // 64 - 10: the first position outside the scan
        PutInt32(chunk, 55, 1000);

        Lzx::UndoE8Translation(gsl::make_span(chunk));
        Assert::AreEqual(1000 - 49, GetInt32(chunk, 50));
        Assert::AreEqual(1000, GetInt32(chunk, 55));

        std::vector<uint8_t> tiny(10, 0xE8);
        Lzx::UndoE8Translation(gsl::make_span(tiny));
        Assert::IsTrue(std::all_of(tiny.begin(), tiny.end(), [](uint8_t byte) { return byte == 0xE8; }));
    }
};

TEST_CLASS(LzxChunkDecoderTest)
{
private:
    static std::vector<uint8_t> ReadChunk0() { return LzxTestChunk(LzxReadKnownPlaintextBlob(), 7, 0); }

    static std::vector<uint8_t> KnownPlaintext(size_t size)
    {
        std::vector<uint8_t> plain(size);
        for (size_t i = 0; i < size; ++i)
        {
            plain[i] = static_cast<uint8_t>((i * 31 + 7) % 251);
        }

        return plain;
    }

public:
    // The whole point of the task: chunk 0 of a file 'compact /c /exe:lzx' produced must decode to the first
    // 32768 bytes of the plaintext, byte for byte.
    TEST_METHOD(DecodesTheKnownPlaintextChunk)
    {
        const std::vector<uint8_t> chunk = ReadChunk0();
        const std::vector<uint8_t> expected = KnownPlaintext(32768);

        std::vector<uint8_t> output(32768, 0xCC);
        Lzx::ChunkDecoder decoder;
        std::error_code ec;
        const size_t produced = decoder.Decode(BufferView(chunk.data(), chunk.size()), gsl::make_span(output), ec);

        Assert::IsFalse(static_cast<bool>(ec));
        Assert::AreEqual(size_t(32768), produced);

        const auto mismatch = std::mismatch(output.begin(), output.end(), expected.begin());
        Assert::IsTrue(
            mismatch.first == output.end(),
            (L"decoded chunk differs from the known plaintext at offset "
             + std::to_wstring(mismatch.first - output.begin()))
                .c_str());
    }

    // Review Focus 3: a chunk that cannot fill the output must set 'ec', never hand back a short buffer the
    // caller cannot tell from success. This is the all zeros failure mode in a new disguise.
    TEST_METHOD(ATruncatedChunkFailsClosedInsteadOfReturningShort)
    {
        std::vector<uint8_t> chunk = ReadChunk0();
        chunk.resize(chunk.size() / 2);

        std::vector<uint8_t> output(32768, 0xCC);
        Lzx::ChunkDecoder decoder;
        std::error_code ec;
        const size_t produced = decoder.Decode(BufferView(chunk.data(), chunk.size()), gsl::make_span(output), ec);

        Assert::IsTrue(static_cast<bool>(ec));
        Assert::AreEqual(size_t(0), produced);
    }

    // A block type outside 1..3 is refused before anything is written.
    TEST_METHOD(AnUnknownBlockTypeIsRefused)
    {
        std::vector<uint8_t> chunk = ReadChunk0();
        chunk[0] = 0x00;
        chunk[1] = 0x00;

        std::vector<uint8_t> output(32768, 0xCC);
        Lzx::ChunkDecoder decoder;
        std::error_code ec;
        static_cast<void>(decoder.Decode(BufferView(chunk.data(), chunk.size()), gsl::make_span(output), ec));
        Assert::IsTrue(static_cast<bool>(ec));
    }

    // Review Focus 5: an uncompressed block (type 3) whose 12 byte recent offset header does not fit the input
    // must fail closed rather than reading past it. Four bytes hold the header and its zero padding and nothing
    // else, so the recent offsets have nowhere to come from.
    TEST_METHOD(AnUncompressedBlockWithNoRoomForItsHeaderIsRefused)
    {
        LzxTestBitWriter writer;
        writer.WriteBits(Lzx::kBlockUncompressed, Lzx::kBlockTypeBits);
        writer.WriteBits(0, 1);  // not the default size: a 16 bit size follows
        writer.WriteBits(16, Lzx::kBlockSizeBits);  // a 16 byte block, whose 12 byte header already does not fit
        const std::vector<uint8_t> chunk = writer.Finish();

        std::vector<uint8_t> output(32768, 0xCC);
        Lzx::ChunkDecoder decoder;
        std::error_code ec;
        const size_t produced = decoder.Decode(BufferView(chunk.data(), chunk.size()), gsl::make_span(output), ec);

        Assert::IsTrue(static_cast<bool>(ec));
        Assert::AreEqual(size_t(0), produced);
    }

    // A recent offset of zero names no byte, and an uncompressed block is the one place they arrive verbatim.
    TEST_METHOD(AnUncompressedBlockWithAZeroRecentOffsetIsRefused)
    {
        LzxTestBitWriter writer;
        writer.WriteBits(Lzx::kBlockUncompressed, Lzx::kBlockTypeBits);
        writer.WriteBits(0, 1);
        writer.WriteBits(2, Lzx::kBlockSizeBits);  // two payload bytes
        std::vector<uint8_t> chunk = writer.Finish();

        // 12 zero bytes of recent offsets, then the payload.
        chunk.insert(chunk.end(), 12, 0x00);
        chunk.push_back(0xAA);
        chunk.push_back(0xBB);

        std::vector<uint8_t> output(32768, 0xCC);
        Lzx::ChunkDecoder decoder;
        std::error_code ec;
        static_cast<void>(decoder.Decode(BufferView(chunk.data(), chunk.size()), gsl::make_span(output), ec));
        Assert::IsTrue(static_cast<bool>(ec));
    }

    // The forged input bar: every single byte mutation either decodes or fails cleanly, and
    // none of them reads out of bounds. Deterministic so a failure is reproducible from the seed.
    TEST_METHOD(EverySingleByteMutationFailsCleanly)
    {
        const std::vector<uint8_t> original = ReadChunk0();

        std::mt19937 generator(20261004);
        for (int iteration = 0; iteration < 2000; ++iteration)
        {
            std::vector<uint8_t> chunk = original;
            const size_t index = generator() % chunk.size();
            chunk[index] = static_cast<uint8_t>(generator() & 0xFF);

            std::vector<uint8_t> output(32768, 0xCC);
            Lzx::ChunkDecoder decoder;
            std::error_code ec;
            const size_t produced =
                decoder.Decode(BufferView(chunk.data(), chunk.size()), gsl::make_span(output), ec);

            // Either it decoded the whole chunk or it reported why. Nothing in between.
            if (!ec)
            {
                Assert::AreEqual(size_t(32768), produced, std::to_wstring(iteration).c_str());
            }
            else
            {
                Assert::AreEqual(size_t(0), produced, std::to_wstring(iteration).c_str());
            }
        }
    }
};

TEST_CLASS(LzxDecompressorConceptTest)
{
public:
    // Review Focus 2: the last chunk of a file is shorter than a window. The decompressor must produce exactly
    // the bytes asked for and report that count, not run on to 32768.
    TEST_METHOD(ThePartialLastChunkProducesExactlyItsSize)
    {
        // The fixture's last chunk: 200000 - 6 * 32768 = 3392 bytes, sliced from the blob committed in Task 2.
        const std::vector<uint8_t> chunk = LzxTestChunk(LzxReadKnownPlaintextBlob(), 7, 6);
        std::vector<uint8_t> output(3392, 0xCC);

        LzxDecompressorConcept decompressor;
        std::error_code ec;
        const size_t produced =
            decompressor.Decompress(BufferView(chunk.data(), chunk.size()), gsl::make_span(output), ec);

        Assert::IsFalse(static_cast<bool>(ec));
        Assert::AreEqual(size_t(3392), produced);

        for (size_t i = 0; i < output.size(); ++i)
        {
            const size_t absolute = 6 * 32768 + i;
            Assert::AreEqual(
                static_cast<int>((absolute * 31 + 7) % 251), static_cast<int>(output[i]), std::to_wstring(i).c_str());
        }
    }

    TEST_METHOD(AnEmptyInputFailsClosed)
    {
        std::vector<uint8_t> output(32768, 0xCC);
        LzxDecompressorConcept decompressor;
        std::error_code ec;
        Assert::AreEqual(size_t(0), decompressor.Decompress(BufferView {}, gsl::make_span(output), ec));
        Assert::IsTrue(static_cast<bool>(ec));
    }

    TEST_METHOD(AnEmptyOutputFailsClosed)
    {
        const std::array<uint8_t, 4> chunk {0, 0, 0, 0};
        LzxDecompressorConcept decompressor;
        std::error_code ec;
        static_cast<void>(decompressor.Decompress(BufferView(chunk.data(), chunk.size()), gsl::span<uint8_t>(), ec));
        Assert::IsTrue(static_cast<bool>(ec));
    }
};

}  // namespace Orc::Test
