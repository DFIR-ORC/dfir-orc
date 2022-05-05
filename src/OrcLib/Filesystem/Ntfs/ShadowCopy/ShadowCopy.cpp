//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "ShadowCopy.h"

#include "Filesystem/Ntfs/ShadowCopy/SnapshotsIndex.h"
#include "Filesystem/Ntfs/ShadowCopy/SnapshotsIndexHeader.h"
#include "Filesystem/Ntfs/ShadowCopy/DiffAreaTable.h"
#include "Filesystem/Ntfs/ShadowCopy/DiffAreaLocationTable.h"
#include "Filesystem/Ntfs/ShadowCopy/DiffAreaBitmap.h"
#include "Filesystem/Ntfs/ShadowCopy/Snapshot.h"
#include "Stream/StreamUtils.h"
#include "Text/Fmt/ByteQuantity.h"
#include "Text/Fmt/FILETIME.h"
#include "Text/Fmt/GUID.h"
#include "Text/Fmt/Offset.h"

using namespace Orc::Ntfs::ShadowCopy;
using namespace Orc::Ntfs;
using namespace Orc;

namespace {

static const auto kShadowCopyBlockSize = DiffAreaTableEntry::kDataSize;

class Chunk
{
public:
    enum class Type
    {
        kNull,
        kCurrentVolume,
        kCopyOnWrite,
        kOverlay,
        kZeroes
    };

    static const uint16_t kSize = 512;

public:
    Type type;
    uint64_t offset;  // Disk offset
    uint64_t endOffset;
    uint16_t offsetInBlock;
    size_t length;
};

using Chunks = std::array<Chunk, 32>;

class ReadParameters final
{
public:
    ReadParameters(uint64_t readOffset, size_t readLength)
        : m_firstBlockOffset(0)
        , m_firstBlockBitmap(0)
        , m_lastBlockBitmap(0)
        , m_blockCount(0)
    {
        if (readLength == 0)
        {
            return;
        }

        const auto kBlockSize = DiffAreaTableEntry::kDataSize;
        const size_t kMinSectorSize = 512;
        const auto kBitmapItemSize = kMinSectorSize;

        m_firstBlockOffset = readOffset & (~(kBlockSize - 1));

        // Length with read offset being aligned on block boundary
        const size_t alignedReadLength = readLength + (readOffset - m_firstBlockOffset);

        const size_t lastBlockResidue = alignedReadLength % kBlockSize;
        m_blockCount = alignedReadLength / kBlockSize + (lastBlockResidue != 0);

        uint32_t ignoredSubBlockCount = (readOffset - m_firstBlockOffset) / kMinSectorSize;

        // Each bit represents a 512 bytes 'sub-block' in a 16384 bytes 'block'
        uint32_t firstBlockBitmap = ~((1 << ignoredSubBlockCount) - 1);

        const size_t residueSubBlockCount =
            lastBlockResidue / kMinSectorSize + ((lastBlockResidue % kMinSectorSize) != 0);
        uint32_t lastBlockBitmap = (1 << residueSubBlockCount) - 1;
        if (lastBlockBitmap == 0)
        {
            lastBlockBitmap = 0xFFFFFFFF;
        }

        if (m_blockCount == 1)
        {
            m_firstBlockBitmap = firstBlockBitmap & lastBlockBitmap;
            m_lastBlockBitmap = m_firstBlockBitmap;
        }
        else
        {
            m_firstBlockBitmap = firstBlockBitmap;
            m_lastBlockBitmap = lastBlockBitmap;
        }
    }

    size_t BlockCount() const { return m_blockCount; }

    uint64_t GetBlockOffset(size_t blockIndex) const { return m_firstBlockOffset + (blockIndex << 14); }

    uint32_t GetReadBitmap(size_t blockIndex) const
    {
        if (blockIndex == 0)
        {
            return m_firstBlockBitmap;
        }
        else if (blockIndex < m_blockCount)
        {
            return 0xFFFFFFFF;
        }
        else if (blockIndex == m_blockCount)
        {
            return m_lastBlockBitmap;
        }

        return 0;
    }

private:
    uint64_t m_firstBlockOffset;
    uint32_t m_firstBlockBitmap;
    uint32_t m_lastBlockBitmap;
    size_t m_blockCount;
};

void BitmapToChunks(Chunk::Type type, uint32_t bitmap, uint64_t offset, Chunks& chunks)
{
    if (bitmap == 0xFFFFFFFF)
    {
        chunks[0] = {type, offset, offset + kShadowCopyBlockSize, 0, kShadowCopyBlockSize};
        return;
    }

    uint8_t i = 0;
    while (i < 32)
    {
        for (; i < 32; ++i)
        {
            if ((1 << i) & bitmap)
            {
                break;
            }
        }

        if (i == 32)
        {
            break;
        }

        uint8_t j = i + 1;
        for (; j < 32; ++j)
        {
            if (!((1 << j) & bitmap))
            {
                break;
            }
        }

        const uint16_t offsetInBlock = i * 512;
        const uint16_t length = (j - i) * 512;
        const uint64_t startOffset = offset + offsetInBlock;
        const uint64_t endOffset = startOffset + length;

        chunks[i] = {type, startOffset, endOffset, offsetInBlock, length};

        i = j;
    }
}

void GetChunksToRead(
    const Orc::Ntfs::ShadowCopy::ShadowCopy& shadowCopy,
    uint64_t blockOffset,
    uint32_t readBitmap,
    Chunks& chunks)
{
    uint32_t overlayBitmap;
    uint32_t cowBitmap;

    chunks = {};

    std::optional<Orc::Ntfs::ShadowCopy::ShadowCopy::Block::Overlay> overlay;
    std::optional<Orc::Ntfs::ShadowCopy::ShadowCopy::Block::CopyOnWrite> cow;
    shadowCopy.GetBlockDescriptors(blockOffset, overlay, cow);

    if (overlay)
    {
        overlayBitmap = readBitmap & overlay->bitmap;
        cowBitmap = readBitmap & (~overlayBitmap);
    }
    else
    {
        overlayBitmap = 0;
        cowBitmap = readBitmap;
    }

    // Convert the overlay and cow bitmaps
    if (overlayBitmap)
    {
        Log::Trace("VSS: Using overlay (block: {:#016x}, bitmap: {:#x})", blockOffset, overlayBitmap);
        BitmapToChunks(Chunk::Type::kOverlay, overlayBitmap, overlay->offset, chunks);
    }

    if (cowBitmap)
    {
        if (cow)
        {
            Log::Trace(
                "VSS: Using cow entry (block: {:#016x}, bitmap: {:#x}, redirection: {:#016x})",
                blockOffset,
                cowBitmap,
                cow->offset);

            BitmapToChunks(Chunk::Type::kCopyOnWrite, cowBitmap, cow->offset, chunks);
            return;
        }

        const auto blockIndex = blockOffset >> 14;  // 14 as block are aligned on 16384 addresses
        const auto bitmapIndex = blockIndex / 8;
        const auto bitIndex = 1 << blockIndex % 8;

        if (bitmapIndex >= shadowCopy.Bitmap().size())
        {
            Log::Debug("VSS: bitmap index out of boundaries");
            BitmapToChunks(Chunk::Type::kZeroes, cowBitmap, blockOffset, chunks);
            return;
        }

        if (shadowCopy.Bitmap()[bitmapIndex] & bitIndex)
        {
            Log::Trace("VSS: Missing cow entry (block: {:#016x}, bitmap: {:#x})", blockOffset, cowBitmap);
            BitmapToChunks(Chunk::Type::kZeroes, cowBitmap, blockOffset, chunks);
            return;
        }

        Log::Trace("VSS: Chunk from current volume (block: {:#016x}, bitmap: {:#x})", blockOffset, cowBitmap);
        BitmapToChunks(Chunk::Type::kCurrentVolume, cowBitmap, blockOffset, chunks);
    }
}

void GetChunksToRead(
    const Orc::Ntfs::ShadowCopy::ShadowCopy& shadowCopy,
    const ReadParameters& blocks,
    size_t blockIndex,
    Chunks& chunks)
{
    auto blockBitmap = blocks.GetReadBitmap(blockIndex);
    if (!blockBitmap)
    {
        return;
    }

    const auto offset = blocks.GetBlockOffset(blockIndex);
    GetChunksToRead(shadowCopy, offset, blockBitmap, chunks);
}

}  // namespace

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

bool HasSnapshotsIndex(StreamReader& stream, std::error_code& ec)
{
    std::array<uint8_t, sizeof(Node::Layout)> buffer;

    stream.Seek(SeekDirection::kBegin, SnapshotsIndexHeader::kVolumeOffset, ec);
    if (ec)
    {
        Log::Debug("Failed to seek to position: {} [{}]", Traits::Offset(SnapshotsIndexHeader::kVolumeOffset), ec);
        ec.clear();
        return false;
    }

    ReadChunk(stream, buffer, ec);
    if (ec)
    {
        Log::Debug("Failed to read VSS header [{}]", ec);
        return false;
    }

    auto& layout = *reinterpret_cast<const Node::Layout*>(buffer.data());
    Node node;
    Node::Parse(layout, node, ec);
    if (ec)
    {
        ec.clear();
        return false;
    }

    return true;
}

void GetShadowCopiesInformation(
    StreamReader& stream,
    std::vector<ShadowCopyInformation>& shadowCopiesInfo,
    std::error_code& ec)
{
    Orc::Ntfs::ShadowCopy::SnapshotsIndex SnapshotsIndex;
    Orc::Ntfs::ShadowCopy::SnapshotsIndex::Parse(stream, SnapshotsIndex, ec);
    if (ec)
    {
        Log::Debug(L"Failed to parse shadow copies [{}]", ec);
        return;
    }

    if (SnapshotsIndex.Items().empty())
    {
        Log::Debug("No shadow copy");
        return;
    }

    for (const auto& snapshot : SnapshotsIndex.Items())
    {
        shadowCopiesInfo.emplace_back(snapshot);
    }

    std::sort(
        std::begin(shadowCopiesInfo),
        std::end(shadowCopiesInfo),
        [](const ShadowCopyInformation& lhs, const ShadowCopyInformation& rhs) {
            return *(reinterpret_cast<const LONGLONG*>(&lhs.CreationTime()))
                < *(reinterpret_cast<const LONGLONG*>(&rhs.CreationTime()));
        });
}

void ShadowCopy::Initialize(gsl::span<const Snapshot> snapshots, ShadowCopy& shadowCopy, std::error_code& ec)
{
    const auto& activeSnapshot = snapshots[0];

    shadowCopy.Information() = ShadowCopyInformation(activeSnapshot.Information());

    if (!activeSnapshot.PreviousBitmap().empty() && activeSnapshot.Bitmap().size() != activeSnapshot.PreviousBitmap().size())
    {
        // This should not be handled as an error. The bits over bitmap size should be seen as set.
        Log::Debug(
            "VSS has inconsitent bitmap size (snapshot: {}, bitmap size: {}, previous bitmap size: {})",
            activeSnapshot.Information().ShadowCopyId(),
            activeSnapshot.Bitmap().size(),
            activeSnapshot.PreviousBitmap().size());
    }

    if (activeSnapshot.PreviousBitmap().empty())
    {
        shadowCopy.m_bitmap.resize(activeSnapshot.Bitmap().size());
        std::copy(
            std::cbegin(activeSnapshot.Bitmap()), std::cend(activeSnapshot.Bitmap()), std::begin(shadowCopy.m_bitmap));
    }
    else
    {
        shadowCopy.m_bitmap.resize(std::max(activeSnapshot.Bitmap().size(), activeSnapshot.PreviousBitmap().size()));
        std::fill(std::begin(shadowCopy.m_bitmap), std::end(shadowCopy.m_bitmap), 0xFF);

        for (size_t i = 0; i < activeSnapshot.Bitmap().size() && i < activeSnapshot.PreviousBitmap().size(); ++i)
        {
            shadowCopy.m_bitmap[i] = activeSnapshot.Bitmap()[i] & activeSnapshot.PreviousBitmap()[i];
        }
    }

    if (snapshots.size() == 1)
    {
        // The last snapshot's bitmap must unset bits for unresolved forwarders
        for (const auto& [offset, forwarder] : activeSnapshot.Forwarders())
        {
            const auto blockIndex = offset >> 14;
            const auto bitmapIndex = blockIndex / 8;
            const auto bitIndex = 1 << blockIndex % 8;

            shadowCopy.m_bitmap[bitmapIndex] = shadowCopy.m_bitmap[bitmapIndex] & ~bitIndex;
        }
    }

    uint64_t copyOnWriteCount = 0;
    for (size_t i = 0; i < snapshots.size(); ++i)
    {
        for (const auto [offset, cow] : snapshots[i].CopyOnWrites())
        {
            auto it = shadowCopy.m_blocks.find(offset);
            if (it == std::cend(shadowCopy.m_blocks))
            {
                shadowCopy.m_blocks.emplace(
                    offset, Block {std::optional<Block::Overlay> {}, Block::CopyOnWrite {cow.offset}});
                ++copyOnWriteCount;
            }
            else if (!it->second.m_copyOnWrite.has_value())
            {
                it->second.m_copyOnWrite = Block::CopyOnWrite {cow.offset};
                ++copyOnWriteCount;
            }
        }
    }

    shadowCopy.Information().SetCopyOnWriteCount(copyOnWriteCount);

    uint64_t overlayCount = 0;
    for (const auto& [offset, overlay] : activeSnapshot.Overlays())
    {
        auto it = shadowCopy.m_blocks.find(offset);
        if (it == std::cend(shadowCopy.m_blocks))
        {
            shadowCopy.m_blocks.emplace(
                offset, Block {Block::Overlay {overlay.offset, overlay.bitmap}, std::optional<Block::CopyOnWrite> {}});
            ++overlayCount;
        }
        else
        {
            it->second.m_overlay = Block::Overlay {overlay.offset, overlay.bitmap};
            ++overlayCount;
        }
    }

    shadowCopy.Information().SetOverlayCount(overlayCount);
}

void ShadowCopy::Parse(StreamReader& stream, std::vector<ShadowCopy>& shadowCopies, std::error_code& ec)
{
    std::vector<Snapshot> snapshots;
    Snapshot::Parse(stream, snapshots, ec);
    if (ec)
    {
        Log::Debug("Failed to parse vss snapshots [{}]", ec);
        return;
    }

    if (snapshots.empty())
    {
        Log::Debug("No snapshots found [{}]", ec);
        return;
    }

    // Resolve forwarders from the older snapshot to the most recent one and store them as cow
    Snapshot::ResolveForwarders(snapshots);

    for (size_t i = 0; i < snapshots.size(); ++i)
    {
        ShadowCopy shadowCopy;
        Initialize(gsl::span<const Snapshot>(&snapshots[i], snapshots.size() - i), shadowCopy, ec);
        if (ec)
        {
            Log::Debug("Failed to parse shadow copy {}", snapshots[0].Information().ShadowCopyId());
            return;
        }

        shadowCopies.push_back(std::move(shadowCopy));
    }
}

void ShadowCopy::Parse(StreamReader& stream, const GUID& shadowCopyId, ShadowCopy& shadowCopy, std::error_code& ec)
{
    SnapshotsIndex snapshotsIndex;
    SnapshotsIndex::Parse(stream, snapshotsIndex, ec);
    if (ec)
    {
        Log::Debug("Failed to read snapshots index while searching for {} [{}]", shadowCopyId, ec);
        return;
    }

    if (snapshotsIndex.Items().empty())
    {
        Log::Debug("No snapshot found");
        return;
    }

    std::vector<SnapshotInformation> snapshotsSelection;
    std::optional<size_t> activeSnapshotPosition;

    for (int i = snapshotsIndex.Items().size() - 1; i >= 0; --i)
    {
        if (snapshotsIndex.Items()[i].ShadowCopyId() == shadowCopyId)
        {
            activeSnapshotPosition = i;
            break;
        }
    }

    if (!activeSnapshotPosition)
    {
        Log::Debug("Failed to find shadow copy in snapshots index (id: {})", shadowCopyId);
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return;
    }

    std::copy(
        std::cbegin(snapshotsIndex.Items()) + *activeSnapshotPosition,
        std::cend(snapshotsIndex.Items()),
        std::back_inserter(snapshotsSelection));

    std::vector<Snapshot> snapshots;
    snapshots.resize(snapshotsSelection.size());

    for (size_t i = 0; i < snapshots.size(); ++i)
    {
        Snapshot::Parse(stream, snapshotsSelection[i], snapshots[i], ec);
        if (ec)
        {
            Log::Debug("Failed to parse a vss snapshot from {} [{}]", shadowCopyId, ec);
            return;
        }
    }

    // Resolve forwarders from the older snapshot to the most recent one and store them as cow
    Snapshot::ResolveForwarders(snapshots);

    Initialize(snapshots, shadowCopy, ec);
    if (ec)
    {
        Log::Debug("Failed to parse shadow copy {}", snapshots[0].Information().ShadowCopyId());
        return;
    }
}

//
// This implementation tries to optimize IO and use direct buffer writes. It aligned and split the reads in 16k block
// with eventually 512 bytes chunks each. Chunks will be merged if their offset, size and type allow that.
//
// Read are more effective when aligned on 16k blocks offsets
//
size_t ShadowCopy::ReadAt(StreamReader& stream, uint64_t offset, BufferSpan output, std::error_code& ec) const
{
    // Divide the read in multiple and aligned blocks
    const ReadParameters readParameters(offset, output.size());

    // Holds the last met chunk to be able to eventually merge it with the next one to limit IO
    bool firstRead = true;
    size_t totalRead = 0;

    auto fnReadChunk = [offset, &firstRead, &totalRead](
                           StreamReader& stream, Chunk& chunk, gsl::span<uint8_t> output, std::error_code& ec) {
        if (firstRead)
        {
            firstRead = false;

            const auto offsetInChunk = offset % Chunk::kSize;
            if (offsetInChunk)
            {
                chunk.offset += offsetInChunk;
                chunk.length -= offsetInChunk;
            }
        }

        auto buffer = output.subspan(totalRead, std::min(chunk.length, output.size() - totalRead));

        size_t processed;
        if (chunk.type == Chunk::Type::kZeroes)
        {
            std::fill(std::begin(buffer), std::end(buffer), 0x00);
            processed = buffer.size();
        }
        else
        {
            processed = Stream::ReadChunkAt(stream, chunk.offset, buffer, ec);
            if (ec)
            {
                return;
            }
        }

        totalRead += processed;
    };

    Chunk pendingChunkToRead = {};
    for (size_t i = 0; i < readParameters.BlockCount(); ++i)
    {
        Chunks chunks;
        GetChunksToRead(*this, readParameters.GetBlockOffset(i), readParameters.GetReadBitmap(i), chunks);

        for (const auto& chunk : chunks)
        {
            if (chunk.length == 0)
            {
                continue;
            }

            // Merge chunks to limit IO
            if (pendingChunkToRead.length)
            {
                if (pendingChunkToRead.endOffset == chunk.offset && pendingChunkToRead.type == chunk.type)
                {
                    pendingChunkToRead.length += chunk.length;
                    pendingChunkToRead.endOffset = chunk.endOffset;
                    continue;
                }

                fnReadChunk(stream, pendingChunkToRead, output, ec);
            }

            pendingChunkToRead = chunk;
        }
    }

    if (pendingChunkToRead.length)
    {
        fnReadChunk(stream, pendingChunkToRead, output, ec);
    }

    return totalRead;
}

void ShadowCopy::GetBlockDescriptors(
    BlockOffset offset,
    std::optional<Block::Overlay>& overlay,
    std::optional<Block::CopyOnWrite>& copyOnWrite) const
{
    auto it = m_blocks.find(offset);
    if (it == std::cend(m_blocks))
    {
        return;
    }

    overlay = it->second.m_overlay;
    copyOnWrite = it->second.m_copyOnWrite;
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
