//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <system_error>

#include "Filesystem/Ntfs/Compression/WofAlgorithm.h"
#include "Utils/BufferView.h"

namespace Orc {
namespace Ntfs {

[[nodiscard]] uint32_t GetWofChunkSize(WofAlgorithm algorithm);
[[nodiscard]] uint64_t GetWofChunkCount(WofAlgorithm algorithm, uint64_t uncompressedSize);
[[nodiscard]] uint8_t GetWofChunkOffsetSize(uint64_t uncompressedSize);
[[nodiscard]] uint32_t GetWofLastChunkSize(uint64_t uncompressedSize, uint32_t chunkSize);

class WofChunks
{
public:
    static constexpr uint16_t kDefaultChunkSize = 16384;

    struct ChunkLocation
    {
        uint64_t offset;
        uint32_t size;
    };

    WofChunks(WofAlgorithm algorithm, uint64_t compressedSize, uint64_t uncompressedSize);

    void ParseLocations(BufferView buffer, gsl::span<ChunkLocation> table, std::error_code& ec) const;

    [[nodiscard]] WofAlgorithm Algorithm() const noexcept { return m_algorithm; }
    [[nodiscard]] uint64_t Size() const noexcept { return m_compressedSize; }
    [[nodiscard]] uint64_t UncompressedSize() const noexcept { return m_uncompressedSize; }
    [[nodiscard]] uint64_t ChunkCount() const noexcept { return m_chunkCount; }
    [[nodiscard]] uint32_t ChunkSize() const noexcept { return m_chunkSize; }
    [[nodiscard]] uint32_t GetLastChunkSize() const { return GetWofLastChunkSize(m_uncompressedSize, m_chunkSize); }
    [[nodiscard]] uint64_t GetChunkTableSize() const
    {
        return GetWofChunkOffsetSize(m_uncompressedSize) * m_chunkCount;
    }
    [[nodiscard]] uint8_t GetChunkOffsetSize() const { return GetWofChunkOffsetSize(m_uncompressedSize); }

private:
    WofAlgorithm m_algorithm;
    uint64_t m_compressedSize;
    uint64_t m_uncompressedSize;
    uint32_t m_chunkSize;

    // 'GetWofChunkCount' counts on 64 bits: a 32 bit member silently truncated a file large enough to need more
    // chunks than a uint32 can index, which would have parsed a table for the wrong number of chunks.
    uint64_t m_chunkCount;
};

}  // namespace Ntfs
}  // namespace Orc
