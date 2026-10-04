//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "LzxCodeLengths.h"

#include <algorithm>
#include <array>

#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxHuffmanTable.h"
#include "Log/Log.h"
#include "Text/Fmt/std_error_code.h"

namespace {

using namespace Orc;
using namespace Orc::Ntfs::Lzx;

constexpr uint32_t kMaxLengthSymbol = 16;
constexpr uint32_t kRunZerosSmall = 17;
constexpr uint32_t kRunZerosLarge = 18;
constexpr uint32_t kRunSameLength = 19;

constexpr uint8_t kPretreeLengthBits = 4;
constexpr uint8_t kRunZerosSmallBits = 4;
constexpr uint32_t kRunZerosSmallBase = 4;
constexpr uint8_t kRunZerosLargeBits = 5;
constexpr uint32_t kRunZerosLargeBase = 20;
constexpr uint8_t kRunSameLengthBits = 1;
constexpr uint32_t kRunSameLengthBase = 4;

// The delta is modulo 17 so a length only ever moves within 0..16.
uint8_t ApplyDelta(uint8_t previous, uint32_t symbol)
{
    return static_cast<uint8_t>((previous + kMaxLengthSymbol + 1 - symbol) % (kMaxLengthSymbol + 1));
}

// A run is never clamped: a count past the requested range means the stream is forged, and a table built from a
// truncated run decodes plausible looking garbage rather than failing, which is the worst outcome available.
bool WriteRun(gsl::span<uint8_t> lengths, size_t& index, size_t runLength, uint8_t value, std::error_code& ec)
{
    if (runLength > lengths.size() - index)
    {
        ec = std::make_error_code(std::errc::bad_message);
        Log::Debug("Lzx: code length run of {} past the {} remaining [{}]", runLength, lengths.size() - index, ec);
        return false;
    }

    std::fill_n(lengths.begin() + index, runLength, value);
    index += runLength;
    return true;
}

bool ReadPretree(BitStream& stream, HuffmanTable& pretree, std::error_code& ec)
{
    std::array<uint8_t, kPretreeSymbolCount> pretreeLengths {};
    for (size_t symbol = 0; symbol < pretreeLengths.size(); ++symbol)
    {
        pretreeLengths[symbol] = static_cast<uint8_t>(stream.ReadBits(kPretreeLengthBits, ec));
        if (ec)
        {
            Log::Debug("Lzx: truncated pretree at symbol {} [{}]", symbol, ec);
            return false;
        }
    }

    if (!pretree.Build(BufferView(pretreeLengths.data(), pretreeLengths.size()), ec))
    {
        Log::Debug("Lzx: pretree is not a valid code [{}]", ec);
        return false;
    }

    return true;
}

}  // namespace

namespace Orc::Ntfs::Lzx {

bool ReadCodeLengths(BitStream& stream, gsl::span<uint8_t> lengths, std::error_code& ec)
{
    if (lengths.empty())
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        Log::Debug("Lzx: refusing to read an empty range of code lengths [{}]", ec);
        return false;
    }

    HuffmanTable pretree;
    if (!ReadPretree(stream, pretree, ec))
    {
        return false;
    }

    size_t index = 0;
    while (index < lengths.size())
    {
        const uint32_t symbol = pretree.Decode(stream, ec);
        if (ec)
        {
            return false;
        }

        if (symbol <= kMaxLengthSymbol)
        {
            lengths[index] = ApplyDelta(lengths[index], symbol);
            ++index;
            continue;
        }

        if (symbol == kRunZerosSmall)
        {
            const uint32_t extra = stream.ReadBits(kRunZerosSmallBits, ec);
            if (ec || !WriteRun(lengths, index, extra + kRunZerosSmallBase, 0, ec))
            {
                return false;
            }

            continue;
        }

        if (symbol == kRunZerosLarge)
        {
            const uint32_t extra = stream.ReadBits(kRunZerosLargeBits, ec);
            if (ec || !WriteRun(lengths, index, extra + kRunZerosLargeBase, 0, ec))
            {
                return false;
            }

            continue;
        }

        if (symbol == kRunSameLength)
        {
            const uint32_t extra = stream.ReadBits(kRunSameLengthBits, ec);
            if (ec)
            {
                return false;
            }

            const uint32_t delta = pretree.Decode(stream, ec);
            if (ec)
            {
                return false;
            }

            if (delta > kMaxLengthSymbol)
            {
                ec = std::make_error_code(std::errc::bad_message);
                Log::Debug("Lzx: pretree symbol {} cannot follow a run of same [{}]", delta, ec);
                return false;
            }

            const uint8_t value = ApplyDelta(lengths[index], delta);
            if (!WriteRun(lengths, index, extra + kRunSameLengthBase, value, ec))
            {
                return false;
            }

            continue;
        }

        ec = std::make_error_code(std::errc::bad_message);
        Log::Debug("Lzx: pretree symbol {} is out of range [{}]", symbol, ec);
        return false;
    }

    return true;
}

}  // namespace Orc::Ntfs::Lzx
