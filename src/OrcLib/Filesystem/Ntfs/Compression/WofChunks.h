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

uint32_t GetWofChunkSize(WofAlgorithm algorithm);
uint64_t GetWofChunkCount(WofAlgorithm algorithm, uint64_t uncompressedSize);
uint8_t GetWofChunkOffsetSize(uint64_t uncompressedSize);
uint32_t GetWofLastChunkSize(uint64_t uncompressedSize, uint32_t chunkSize);

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

    WofAlgorithm Algorithm() const { return m_algorithm; }
    uint64_t Size() const { return m_compressedSize; }
    uint64_t UncompressedSize() const { return m_uncompressedSize; }
    uint64_t ChunkCount() const { return m_chunkCount; }
    uint32_t ChunkSize() const { return m_chunkSize; }
    uint32_t GetLastChunkSize() const { return GetWofLastChunkSize(m_uncompressedSize, m_chunkSize); }
    uint64_t GetChunkTableSize() const { return GetWofChunkOffsetSize(m_uncompressedSize) * m_chunkCount; }
    uint8_t GetChunkOffsetSize() const { return GetWofChunkOffsetSize(m_uncompressedSize); }
    WofAlgorithm Format() const { return m_algorithm; }

private:
    WofAlgorithm m_algorithm;
    uint64_t m_compressedSize;
    uint64_t m_uncompressedSize;
    uint32_t m_chunkSize;
    uint32_t m_chunkCount;
};

}  // namespace Ntfs
}  // namespace Orc
