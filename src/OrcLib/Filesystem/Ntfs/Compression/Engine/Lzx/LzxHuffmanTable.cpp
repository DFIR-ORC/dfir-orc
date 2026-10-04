//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "LzxHuffmanTable.h"

#include "Log/Log.h"
#include "Text/Fmt/std_error_code.h"

namespace {

using namespace Orc::Ntfs::Lzx;

using CountPerLength = std::array<uint16_t, HuffmanTable::kMaxCodeLength + 1>;

// A code is complete when every leaf is reachable and no two codes overlap. Kraft's inequality says the sum of
// 2^-length over the symbols must be exactly 1; both halves matter, since an over subscribed set aliases symbols
// and an incomplete one leaves bit patterns a forged stream could walk into.
bool IsCodeComplete(const CountPerLength& countPerLength)
{
    uint32_t available = 2;
    for (size_t length = 1; length < countPerLength.size(); ++length)
    {
        if (countPerLength[length] > available)
        {
            return false;
        }

        available = (available - countPerLength[length]) << 1;
    }

    return available == 0;
}

}  // namespace

namespace Orc::Ntfs::Lzx {

bool HuffmanTable::Build(BufferView lengths, std::error_code& ec)
{
    if (lengths.empty() || lengths.size() > kMaxSymbols)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        Log::Debug("Lzx: huffman table of {} symbols refused [{}]", lengths.size(), ec);
        return false;
    }

    m_countPerLength.fill(0);
    m_firstCodePerLength.fill(0);
    m_firstIndexPerLength.fill(0);
    m_symbolCount = 0;

    for (size_t symbol = 0; symbol < lengths.size(); ++symbol)
    {
        if (lengths[symbol] > kMaxCodeLength)
        {
            ec = std::make_error_code(std::errc::bad_message);
            Log::Debug("Lzx: code length {} for symbol {} is out of range [{}]", lengths[symbol], symbol, ec);
            return false;
        }

        ++m_countPerLength[lengths[symbol]];
    }

    if (!IsCodeComplete(m_countPerLength))
    {
        ec = std::make_error_code(std::errc::bad_message);
        Log::Debug("Lzx: huffman code lengths are not a complete code [{}]", ec);
        return false;
    }

    uint32_t code = 0;
    uint16_t index = 0;
    for (size_t length = 1; length <= kMaxCodeLength; ++length)
    {
        m_firstCodePerLength[length] = code;
        m_firstIndexPerLength[length] = index;
        index = static_cast<uint16_t>(index + m_countPerLength[length]);
        code = (code + m_countPerLength[length]) << 1;
    }

    // Symbols ordered by (length, symbol), which is what the canonical assignment above indexes into.
    uint16_t next = 0;
    for (size_t length = 1; length <= kMaxCodeLength; ++length)
    {
        for (size_t symbol = 0; symbol < lengths.size(); ++symbol)
        {
            if (lengths[symbol] == length)
            {
                m_symbolsByCode[next++] = static_cast<uint16_t>(symbol);
            }
        }
    }

    m_symbolCount = next;
    return true;
}

uint32_t HuffmanTable::Decode(BitStream& stream, std::error_code& ec) const
{
    uint32_t code = 0;
    for (size_t length = 1; length <= kMaxCodeLength; ++length)
    {
        code = (code << 1) | stream.ReadBits(1, ec);
        if (ec)
        {
            Log::Debug("Lzx: huffman decode ran out of input at length {} [{}]", length, ec);
            return 0;
        }

        if (m_countPerLength[length] == 0)
        {
            continue;
        }

        // Unsigned on purpose: when 'code' is below the first code of this length the difference wraps to a large
        // value and fails the test, which is the intended outcome. Do not "fix" it into a signed comparison.
        const uint32_t first = m_firstCodePerLength[length];
        if (code - first < m_countPerLength[length])
        {
            const size_t index = m_firstIndexPerLength[length] + (code - first);
            if (index >= m_symbolCount)
            {
                ec = std::make_error_code(std::errc::bad_message);
                Log::Debug("Lzx: huffman symbol index {} past {} symbols [{}]", index, m_symbolCount, ec);
                return 0;
            }

            return m_symbolsByCode[index];
        }
    }

    ec = std::make_error_code(std::errc::bad_message);
    Log::Debug("Lzx: no huffman code matched in {} bits [{}]", kMaxCodeLength, ec);
    return 0;
}

}  // namespace Orc::Ntfs::Lzx
