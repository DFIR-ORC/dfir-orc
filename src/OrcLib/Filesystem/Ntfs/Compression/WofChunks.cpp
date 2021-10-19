//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "WofChunks.h"

#include "Log/Log.h"

using namespace Orc::Ntfs;
using namespace Orc;

namespace {

template <typename ChunkOffsetType>
void ParseLocations(
    BufferView& buffer,
    uint64_t compressedSize,
    uint64_t chunkCount,
    gsl::span<WofChunks::ChunkLocation>& table)
{
    using ChunkOffsets = std::basic_string_view<ChunkOffsetType>;

    ChunkOffsets offsets(
        reinterpret_cast<const ChunkOffsetType*>(buffer.data()), buffer.size() / sizeof(ChunkOffsetType));

    auto shift = std::is_same_v<ChunkOffsetType, uint32_t> ? 2 : 3;
    const auto tableSize = (chunkCount - 1) << shift;

    uint64_t tableOffset = 0;

    uint64_t base = tableSize;
    base = base + tableSize;

    if (offsets.size() == 1)
    {
        table[0].offset = 0;
        table[0].size = compressedSize;
    }
    else
    {
        table[0].offset = tableSize;
        table[0].size = offsets[0];

        for (size_t i = 1; i < offsets.size(); ++i)
        {
            table[i].offset = offsets[i - 1] + tableSize;
            table[i].size = offsets[i] - offsets[i - 1];  // wrong for last item but fixed out the loop
        }

        auto& last = table[offsets.size() - 1];
        last.size = compressedSize - last.offset;
    }
}

}  // namespace

namespace Orc {
namespace Ntfs {

enum class WofBitOrder : uint16_t
{
    kUnknown,
    kXpress4k = 12,
    kXpress8k = 13,
    kXpress16k = 14,
    kLzx = 15
};

WofChunks::WofChunks(WofAlgorithm algorithm, uint64_t compressedSize, uint64_t uncompressedSize)
    : m_algorithm(algorithm)
    , m_compressedSize(compressedSize)
    , m_uncompressedSize(uncompressedSize)
    , m_chunkSize(GetWofChunkSize(algorithm))
    , m_chunkCount(GetWofChunkCount(algorithm, uncompressedSize))
{
}
void WofChunks::ParseLocations(BufferView buffer, gsl::span<ChunkLocation> table, std::error_code& ec) const
{
    const auto expectedBufferSize = m_chunkCount * GetChunkOffsetSize();
    if (buffer.size() < expectedBufferSize)
    {
        ec = std::make_error_code(std::errc::message_size);
        Log::Debug(
            "Failed to parse chunk offset table: input buffer is too small (expected: {}, actual: {})",
            expectedBufferSize,
            buffer.size());
        return;
    }

    if (table.size() < m_chunkCount)
    {
        ec = std::make_error_code(std::errc::message_size);
        Log::Debug(
            "Failed to parse chunk offset table: output table is too small (expected: {}, actual: {})",
            m_chunkCount,
            table.size());
        return;
    }

    if (GetChunkOffsetSize() == sizeof(uint64_t))
    {
        ::ParseLocations<uint64_t>(buffer, m_compressedSize, m_chunkCount, table);
    }
    else
    {
        ::ParseLocations<uint32_t>(buffer, m_compressedSize, m_chunkCount, table);
    }
}

WofBitOrder ToWofBitOrder(WofAlgorithm algorithm)
{
    switch (algorithm)
    {
        case WofAlgorithm::kXpress4k:
            return WofBitOrder::kXpress4k;
        case WofAlgorithm::kXpress8k:
            return WofBitOrder::kXpress8k;
        case WofAlgorithm::kXpress16k:
            return WofBitOrder::kXpress16k;
        case WofAlgorithm::kLzx:
            return WofBitOrder::kLzx;
    }

    Log::Debug("ToWofBitOrder: failed conversion from WofAlgorithm with value {}", algorithm);
    return WofBitOrder::kUnknown;
}

uint32_t GetWofChunkSize(WofAlgorithm algorithm)
{
    if (algorithm == WofAlgorithm::kUnknown)
    {
        return 0;
    }

    return 1 << std::underlying_type_t<WofBitOrder>(ToWofBitOrder(algorithm));
}

uint64_t GetWofChunkCount(WofAlgorithm algorithm, uint64_t uncompressedSize)
{
    if (algorithm == WofAlgorithm::kUnknown)
    {
        return 0;
    }

    const auto bit = std::underlying_type_t<WofBitOrder>(ToWofBitOrder(algorithm));
    return (uncompressedSize + GetWofChunkSize(algorithm) - 1) >> bit;
}

uint8_t GetWofChunkOffsetSize(uint64_t uncompressedSize)
{
    if (uncompressedSize >= UINT32_MAX)
    {
        return sizeof(uint64_t);
    }
    else
    {
        return sizeof(uint32_t);
    }
}

uint32_t GetWofLastChunkSize(uint64_t uncompressedSize, uint32_t chunkSize)
{
    return ((uncompressedSize - 1) & (chunkSize - 1)) + 1;
}

}  // namespace Ntfs
}  // namespace Orc
