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

#include "Log/Log.h"
#include "Text/Fmt/std_error_code.h"
#include "Utils/BufferView.h"

namespace Orc::Ntfs::Lzx {

// LZX codes its bitstream as a sequence of 16 bit little endian words whose bits are consumed from the most
// significant end. This is the only type that walks the compressed buffer, so every bound on the input is
// checked here once instead of at each call site.
class BitStream
{
public:
    static constexpr uint8_t kMaxBitsPerRead = 16;

    explicit BitStream(BufferView input)
        : m_input(input)
    {
    }

    // Consumes 'count' bits, most significant first. 'count' is a code constant, never a parsed value.
    [[nodiscard]] uint32_t ReadBits(uint8_t count, std::error_code& ec)
    {
        if (count > kMaxBitsPerRead)
        {
            ec = std::make_error_code(std::errc::invalid_argument);
            Log::Debug("Lzx: refusing to read {} bits at once [{}]", count, ec);
            return 0;
        }

        while (m_bitCount < count)
        {
            if (!FillWord(ec))
            {
                return 0;
            }
        }

        const uint32_t value = static_cast<uint32_t>(m_bitBuffer >> (m_bitCount - count)) & Mask(count);
        m_bitCount -= count;
        m_bitBuffer &= (static_cast<uint64_t>(1) << m_bitCount) - 1;
        return value;
    }

    // Drops the padding before an uncompressed block: 1 to 16 bits, never 0, so on a word boundary a whole word
    // goes. The dropped bits are zero, and anything else is refused: non zero padding means the stream is forged
    // or this reader has already lost synchronisation, and decoding on would produce plausible looking garbage.
    void AlignToWord(std::error_code& ec)
    {
        if (m_bitCount == 0 && !FillWord(ec))
        {
            return;
        }

        if (m_bitBuffer != 0)
        {
            ec = std::make_error_code(std::errc::bad_message);
            Log::Debug("Lzx: {} non zero padding bits before a word boundary [{}]", m_bitCount, ec);
            return;
        }

        m_bitCount = 0;
    }

    // Raw bytes, valid only right after 'AlignToWord'.
    [[nodiscard]] BufferView ReadBytes(size_t count, std::error_code& ec)
    {
        if (m_bitCount != 0)
        {
            ec = std::make_error_code(std::errc::invalid_argument);
            Log::Debug("Lzx: raw bytes requested while {} bits are buffered [{}]", m_bitCount, ec);
            return {};
        }

        if (count > m_input.size() - m_offset)
        {
            ec = std::make_error_code(std::errc::message_size);
            Log::Debug("Lzx: {} raw bytes requested with {} left [{}]", count, m_input.size() - m_offset, ec);
            return {};
        }

        const BufferView bytes = m_input.subspan(m_offset, count);
        m_offset += count;
        return bytes;
    }

    [[nodiscard]] bool Exhausted() const noexcept { return m_bitCount == 0 && m_offset == m_input.size(); }

private:
    // 'count' is at most 'kMaxBitsPerRead', so the shift is always in range.
    [[nodiscard]] static constexpr uint32_t Mask(uint8_t count) noexcept { return (1u << count) - 1; }

    // A word is two bytes: a buffer that cannot supply both is exhausted, and no partial word is invented.
    bool FillWord(std::error_code& ec)
    {
        if (m_input.size() - m_offset < sizeof(uint16_t))
        {
            ec = std::make_error_code(std::errc::message_size);
            Log::Debug("Lzx: bitstream exhausted at offset {} of {} [{}]", m_offset, m_input.size(), ec);
            return false;
        }

        const uint16_t word =
            static_cast<uint16_t>(m_input[m_offset] | (static_cast<uint16_t>(m_input[m_offset + 1]) << 8));
        m_offset += sizeof(uint16_t);

        m_bitBuffer = (m_bitBuffer << 16) | word;
        m_bitCount += 16;
        return true;
    }

    BufferView m_input;
    size_t m_offset = 0;
    uint64_t m_bitBuffer = 0;
    uint8_t m_bitCount = 0;
};

}  // namespace Orc::Ntfs::Lzx
