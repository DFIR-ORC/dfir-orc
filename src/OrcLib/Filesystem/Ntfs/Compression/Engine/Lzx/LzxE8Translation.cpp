//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "LzxE8Translation.h"

namespace {

using namespace Orc::Ntfs::Lzx;

constexpr uint8_t kCallOpcode = 0xE8;
constexpr size_t kOperandSize = sizeof(uint32_t);

uint32_t ReadLittleEndian32(gsl::span<const uint8_t> bytes)
{
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8)
        | (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

void WriteLittleEndian32(gsl::span<uint8_t> bytes, uint32_t value)
{
    for (size_t i = 0; i < kOperandSize; ++i)
    {
        bytes[i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

// Turns the absolute target the encoder wrote back into the displacement the instruction had. The transform is
// defined over signed 32 bit targets and is written that way here: a call displacement is signed, and the two
// ranges below are the only ones an encoder could have produced.
//
//   0 <= target < kE8FileSize  a target inside the notional file, made relative to the opcode's own position
//   -position <= target < 0    a target the encoder carried past zero, brought back by the notional file size
//
// Anything else was never translated and is left exactly as it is. The intermediate is 64 bit so neither branch
// can overflow, which keeps the two conditions readable as the ranges they are.
int32_t ToDisplacement(int32_t target, int32_t position)
{
    if (target >= 0 && target < static_cast<int32_t>(kE8FileSize))
    {
        return static_cast<int32_t>(static_cast<int64_t>(target) - position);
    }

    if (target < 0 && target >= -position)
    {
        return static_cast<int32_t>(static_cast<int64_t>(target) + kE8FileSize);
    }

    return target;
}

}  // namespace

namespace Orc::Ntfs::Lzx {

void UndoE8Translation(gsl::span<uint8_t> chunk)
{
    if (chunk.size() <= kE8Tail)
    {
        return;
    }

    // 'end' is the first position not scanned, so an operand read at 'i + 1 .. i + 4' stays inside the chunk.
    const size_t end = chunk.size() - kE8Tail;
    size_t i = 0;
    while (i < end)
    {
        if (chunk[i] != kCallOpcode)
        {
            ++i;
            continue;
        }

        // The operand is a signed displacement, so it is handled as one; the conversion either way is two's
        // complement on every target this builds for.
        const gsl::span<uint8_t> operand = chunk.subspan(i + 1, kOperandSize);
        const int32_t target = static_cast<int32_t>(ReadLittleEndian32(operand));
        const int32_t displacement = ToDisplacement(target, static_cast<int32_t>(i));
        WriteLittleEndian32(operand, static_cast<uint32_t>(displacement));
        i += 1 + kOperandSize;
    }
}

}  // namespace Orc::Ntfs::Lzx
