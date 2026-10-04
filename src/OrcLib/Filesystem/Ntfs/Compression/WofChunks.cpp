//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "WofChunks.h"

#include <limits>

#include "Log/Log.h"
#include "Text/Fmt/std_error_code.h"

using namespace Orc::Ntfs;
using namespace Orc;

namespace {

// Whole range check, overflow safe: 'size' bytes starting at 'offset' must lie inside 'bufferSize'.
bool IsRangeValid(size_t bufferSize, uint64_t offset, uint64_t size)
{
    return offset <= bufferSize && size <= bufferSize - offset;
}

// One chunk table entry, little endian, read byte by byte. The table is raw bytes off the volume: it carries no
// alignment guarantee for a 64 bit load, and aliasing it with a pointer cast would be undefined.
template <typename ChunkOffsetType>
uint64_t ReadChunkTableEntry(BufferView table, size_t index)
{
    uint64_t value = 0;
    const size_t at = index * sizeof(ChunkOffsetType);
    for (size_t byte = 0; byte < sizeof(ChunkOffsetType); ++byte)
    {
        value |= static_cast<uint64_t>(table[at + byte]) << (8 * byte);
    }

    return value;
}

// The table holds one offset per chunk except the first, each relative to the end of the table: chunk 0 starts
// where the table ends, chunk i where entry i-1 says, and the last chunk runs to the end of the compressed data.
//
// Every offset here comes off the volume. The loop is bound by 'chunkCount', which the caller has already checked
// against both the buffer and the output span, never by the buffer's own length; and each location is validated
// before being written, because an offset or size the compressed stream could not contain would send a later read
// outside it.
template <typename ChunkOffsetType>
void ParseLocations(
    BufferView buffer,
    uint64_t compressedSize,
    uint64_t chunkCount,
    gsl::span<WofChunks::ChunkLocation> table,
    std::error_code& ec)
{
    const uint64_t entryCount = chunkCount - 1;  // 'chunkCount' is at least one, checked by the caller
    const uint64_t tableSize = entryCount * sizeof(ChunkOffsetType);
    if (!IsRangeValid(buffer.size(), 0, tableSize) || chunkCount > table.size())
    {
        ec = std::make_error_code(std::errc::message_size);
        Log::Debug(
            "Failed to parse chunk offset table: {} chunks need {} bytes of {} and {} entries of {} [{}]",
            chunkCount,
            tableSize,
            buffer.size(),
            chunkCount,
            table.size(),
            ec);
        return;
    }

    if (tableSize > compressedSize)
    {
        ec = std::make_error_code(std::errc::bad_message);
        Log::Debug(
            "Failed to parse chunk offset table: table of {} bytes exceeds the {} compressed [{}]",
            tableSize,
            compressedSize,
            ec);
        return;
    }

    uint64_t begin = tableSize;
    for (uint64_t index = 0; index < chunkCount; ++index)
    {
        // The last chunk has no entry of its own: it ends where the compressed data does.
        uint64_t end = compressedSize;
        if (index < entryCount)
        {
            // A wrapped sum lands below 'begin' and is caught with everything else below.
            end = ReadChunkTableEntry<ChunkOffsetType>(buffer, static_cast<size_t>(index)) + tableSize;
        }

        if (end < begin || end > compressedSize)
        {
            ec = std::make_error_code(std::errc::bad_message);
            Log::Debug(
                "Failed to parse chunk offset table: chunk {} spans {} to {} of {} compressed bytes [{}]",
                index,
                begin,
                end,
                compressedSize,
                ec);
            return;
        }

        const uint64_t size = end - begin;
        if (size == 0 || size > std::numeric_limits<uint32_t>::max())
        {
            ec = std::make_error_code(std::errc::bad_message);
            Log::Debug("Failed to parse chunk offset table: chunk {} is {} bytes [{}]", index, size, ec);
            return;
        }

        WofChunks::ChunkLocation& location = table[static_cast<size_t>(index)];
        location.offset = begin;
        location.size = static_cast<uint32_t>(size);
        begin = end;
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
    if (m_chunkCount == 0)
    {
        ec = std::make_error_code(std::errc::message_size);
        Log::Debug("Failed to parse chunk offset table: chunk count is zero [{}]", ec);
        return;
    }

    const uint64_t expectedBufferSize = m_chunkCount * GetChunkOffsetSize();
    if (buffer.size() < expectedBufferSize)
    {
        ec = std::make_error_code(std::errc::message_size);
        Log::Debug(
            "Failed to parse chunk offset table: input buffer is too small (expected: {}, got: {}) [{}]",
            expectedBufferSize,
            buffer.size(),
            ec);
        return;
    }

    if (table.size() < m_chunkCount)
    {
        ec = std::make_error_code(std::errc::message_size);
        Log::Debug(
            "Failed to parse chunk offset table: output table is too small (expected: {}, got: {}) [{}]",
            m_chunkCount,
            table.size(),
            ec);
        return;
    }

    // A file at or past 4GB uncompressed needs 64 bit offsets to address its own compressed data. Either way a
    // failure is reported through 'ec', logged where the offending entry was read.
    if (GetChunkOffsetSize() == sizeof(uint64_t))
    {
        ::ParseLocations<uint64_t>(buffer, m_compressedSize, m_chunkCount, table, ec);
    }
    else
    {
        ::ParseLocations<uint32_t>(buffer, m_compressedSize, m_chunkCount, table, ec);
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

    Log::Debug(
        "ToWofBitOrder: failed conversion from WofAlgorithm (algorithm: {}, id: {})",
        ToString(algorithm),
        std::underlying_type_t<WofAlgorithm>(algorithm));
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

    const uint32_t chunkSize = GetWofChunkSize(algorithm);
    if (chunkSize == 0 || uncompressedSize > UINT64_MAX - (chunkSize - 1))
    {
        return 0;
    }

    return (uncompressedSize + chunkSize - 1) >> bit;
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
    // An empty file has no last chunk, and a chunk size of zero comes from an algorithm with no chunk geometry:
    // both would wrap the mask below into a nonsense size rather than say so.
    if (uncompressedSize == 0 || chunkSize == 0)
    {
        return 0;
    }

    return static_cast<uint32_t>(((uncompressedSize - 1) & (chunkSize - 1)) + 1);
}

}  // namespace Ntfs
}  // namespace Orc
