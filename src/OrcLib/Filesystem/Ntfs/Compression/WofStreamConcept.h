//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <algorithm>

#include <fmt/format.h>

#include "Filesystem/Ntfs/Compression/WofChunks.h"
#include "Stream/SeekDirection.h"
#include "Stream/StreamUtils.h"
#include "Utils/MetaPtr.h"

namespace Orc {
namespace Ntfs {

template <typename InputStreamT, typename DecompressorT>
class WofStreamConcept
{
public:
    using ChunkBufferT = fmt::basic_memory_buffer<uint8_t, WofChunks::kDefaultChunkSize>;

    // Required by ByteStream interface
    WofStreamConcept()
        : m_decompressor()
        , m_stream()
        , m_chunks(WofAlgorithm::kUnknown, 0, 0)
        , m_startOffset(0)
        , m_offset(0)
        , m_chunkIndex(0)
    {
    }

    WofStreamConcept(
        DecompressorT decompressor,
        InputStreamT stream,
        uint64_t offset,
        WofAlgorithm algorithm,
        uint64_t compressedSize,
        uint64_t uncompressedSize,
        std::error_code& ec)
        : m_decompressor(std::move(decompressor))
        , m_stream(std::move(stream))
        , m_chunks(algorithm, compressedSize, uncompressedSize)
        , m_startOffset(offset)
        , m_offset(0)
        , m_chunkIndex(0)
    {
        m_inputBuffer.resize(m_chunks.ChunkSize());
        m_locations.resize(m_chunks.ChunkCount());

        auto loc = gsl::span<WofChunks::ChunkLocation>(m_locations.data(), m_locations.size());
        BuildChunkTable(loc, ec);
    }

    WofStreamConcept(WofStreamConcept&&) = default;
    WofStreamConcept& operator=(WofStreamConcept&&) = default;

    size_t Read(gsl::span<uint8_t> output, std::error_code& ec)
    {
        if (m_offset == m_chunks.UncompressedSize())
        {
            return 0;
        }

        // Handle first chunk separatly as 'output' buffer can be smaller than a chunk or to be shifted
        const auto startIndex = m_offset / m_chunks.ChunkSize();
        const size_t startOffset = m_offset % m_chunks.ChunkSize();
        uint64_t totalProcessed = Decompress(startIndex, startOffset, output, ec);
        if (ec)
        {
            Log::Error("Failed to decompress first chunk #{} [{}]", startIndex, ec);
            return totalProcessed;
        }

        if (output.size() == totalProcessed)
        {
            m_offset += totalProcessed;
            return totalProcessed;
        }

        if (m_offset + totalProcessed == m_chunks.UncompressedSize())
        {
            m_offset = m_chunks.UncompressedSize();
            return totalProcessed;
        }

        // Decompress every other chunks but the last one for which the remaining buffer size could be too small
        auto endIndex = (m_offset + output.size()) / m_chunks.ChunkSize();
        if (endIndex >= m_chunks.ChunkCount())
        {
            endIndex = m_chunks.ChunkCount() - 1;
        }

        for (uint64_t i = startIndex + 1; i < endIndex; ++i)
        {
            auto subspan = output.subspan(totalProcessed, output.size() - totalProcessed);
            const auto processed = DecompressCompleteChunk(i, subspan, ec);
            if (ec)
            {
                Log::Error("Failed to decompress chunk #{} [{}]", i, ec);
                return totalProcessed;
            }

            totalProcessed += processed;
        }

        // Decompress last chunk making sure it will work even if it must be truncated because of output's size
        totalProcessed += Decompress(endIndex, 0, output.subspan(totalProcessed, output.size() - totalProcessed), ec);
        if (ec)
        {
            Log::Error("Failed to decompress last chunk #{} [{}]", endIndex, ec);
            return totalProcessed;
        }

        m_offset += totalProcessed;
        return totalProcessed;
    }

    size_t Write(BufferView& input, std::error_code& ec)
    {
        ec = std::make_error_code(std::errc::function_not_supported);
        return 0;
    }

    uint64_t Seek(SeekDirection direction, int64_t value, std::error_code& ec)
    {
        uint64_t newOffset = 0;

        switch (direction)
        {
            case SeekDirection::kBegin:
                newOffset = value;
                break;
            case SeekDirection::kCurrent:
                newOffset = m_offset + value;
                break;
            case SeekDirection::kEnd:
                newOffset = m_chunks.UncompressedSize() - 1 + value;
                break;
        }

        if (newOffset < 0 || newOffset > m_chunks.UncompressedSize())
        {
            ec = std::make_error_code(std::errc::invalid_seek);
            return -1;
        }

        m_chunkIndex = newOffset / m_chunks.ChunkSize();
        if (m_chunkIndex > m_locations.size() - 1)
        {
            Log::Debug("Failed to seek underlying stream: invalid chunk index");
            ec = std::make_error_code(std::errc::invalid_seek);
            return -1;
        }

        m_stream->Seek(SeekDirection::kBegin, m_locations[m_chunkIndex].offset, ec);
        if (ec)
        {
            Log::Debug("Failed to seek underlying stream [{}]", ec);
            return -1;
        }

        m_offset = newOffset;
        return m_offset - m_startOffset;
    }

    const uint64_t UncompressedSize() const { return m_chunks.UncompressedSize(); }

private:
    void BuildChunkTable(gsl::span<WofChunks::ChunkLocation>& table, std::error_code& ec)
    {
        // A stack buffer of 8192 bytes it should be able to hold 16MB of compressed data
        fmt::basic_memory_buffer<uint8_t, 8192> buffer;
        buffer.resize(m_chunks.GetChunkTableSize());

        Orc::Stream::ReadChunkAt(*m_stream, m_startOffset, buffer, ec);
        if (ec)
        {
            Log::Debug("Failed to read chunk table [{}]", ec);
            return;
        }

        m_chunks.ParseLocations(buffer, m_locations, ec);
        if (ec)
        {
            Log::Debug("Failed to parse chunk table [{}]", ec);
            return;
        }

        const auto kMaxChunkSize = 1 << 20;
        for (size_t i = 0; i < m_locations.size(); ++i)
        {
            if (m_locations[i].size >= kMaxChunkSize)
            {
                Log::Debug("Invalid chunk #{} size: {} (max is {})", i, m_locations[i].size, kMaxChunkSize);
                ec = std::make_error_code(std::errc::bad_message);
                return;
            }
        }
    }

    size_t ReadRawChunk(uint64_t chunkIndex, ChunkBufferT& output, std::error_code& ec)
    {
        if (chunkIndex >= m_locations.size())
        {
            Log::Debug("Invalid chunk index #{}", chunkIndex);
            ec = std::make_error_code(std::errc::result_out_of_range);
            return 0;
        }

        // Use 'ReadChunk' to do as many 'Read()' on the stream as necessary to fill the 'chunk' buffer.
        const auto& chunk = m_locations[chunkIndex];
        output.resize(chunk.size);
        return Orc::Stream::ReadChunkAt(*m_stream, chunk.offset, output, ec);
    }

    // Decompress a chunk and shift the output to 'uncompressedOffset'. Uses an internal buffer if needed.
    size_t Decompress(uint64_t chunkIndex, uint32_t offset, gsl::span<uint8_t> output, std::error_code& ec)
    {
        gsl::span<uint8_t> out = output;
        ChunkBufferT fallback;
        if (output.size() < m_chunks.ChunkSize())
        {
            if (output.size() == 0)
            {
                return 0;
            }

            // Decompress() will expect a buffer of chunk's size
            fallback.resize(m_chunks.ChunkSize());
            out = fallback;
        }

        size_t processed = DecompressCompleteChunk(chunkIndex, out, ec);
        if (ec)
        {
            Log::Error("Failed to decompress chunk #{} [{}]", chunkIndex, ec);
            return 0;
        }

        if (offset > processed)
        {
            Log::Error("Failed to shift decompressed chunk (offset: {}, processed: {})", offset, processed);
            return 0;
        }

        processed = std::min(processed - offset, output.size());
        std::copy_n(std::cbegin(out) + offset, processed, std::begin(output));
        return processed;
    }

    // Decompress a complete a chunk in a big enough output buffer of at least "chunk size"
    size_t DecompressCompleteChunk(uint64_t chunkIndex, gsl::span<uint8_t> output, std::error_code& ec)
    {
        // BEWARE: 'output' must be big enough to store the complete decompressed chunk
        assert(output.size() >= m_chunks.ChunkSize());

        ReadRawChunk(chunkIndex, m_inputBuffer, ec);
        if (ec)
        {
            Log::Debug(
                "Failed to read chunk {}/{} (algorithm: {}, chunk size: {}) [{}]",
                chunkIndex,
                m_chunks.ChunkCount(),
                ToString(m_chunks.Algorithm()),
                m_chunks.ChunkSize(),
                ec);
            return 0;
        }

        size_t outputSize;
        if (chunkIndex == m_chunks.ChunkCount() - 1)
        {
            outputSize = m_chunks.GetLastChunkSize();
        }
        else
        {
            outputSize = std::min(output.size(), static_cast<size_t>(m_chunks.ChunkSize()));
        }

        m_decompressor->Decompress(m_inputBuffer, output.subspan(0, outputSize), ec);
        if (ec)
        {
            Log::Debug(
                "Failed to decompress chunk {}/{} (algorithm: {}, size: {}, buffer size: {}, output size: {}) [{}]",
                chunkIndex,
                m_chunks.ChunkCount(),
                ToString(m_chunks.Algorithm()),
                m_chunks.ChunkSize(),
                m_inputBuffer.size(),
                outputSize,
                ec);
            return 0;
        }

        return outputSize;
    }

private:
    MetaPtr<DecompressorT> m_decompressor;
    MetaPtr<InputStreamT> m_stream;
    WofChunks m_chunks;
    uint64_t m_startOffset;
    uint64_t m_offset;
    uint64_t m_chunkIndex;

    fmt::basic_memory_buffer<WofChunks::ChunkLocation, 8192> m_locations;
    ChunkBufferT m_inputBuffer;
};

}  // namespace Ntfs
}  // namespace Orc
