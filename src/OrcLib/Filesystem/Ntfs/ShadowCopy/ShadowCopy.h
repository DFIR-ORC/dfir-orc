//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "Stream/StreamReader.h"
#include "Utils/BufferSpan.h"
#include "Filesystem/Ntfs/ShadowCopy/ShadowCopyInformation.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class Snapshot;

bool HasSnapshotsIndex(StreamReader& stream, std::error_code& ec);

void GetShadowCopiesInformation(
    StreamReader& stream,
    std::vector<ShadowCopyInformation>& shadowCopyInformation,
    std::error_code& ec);

class ShadowCopy final
{
public:
    using BlockOffset = uint64_t;  // block offsets are aligned on 16384 boundary

    struct Block
    {
        struct Overlay
        {
            BlockOffset offset;
            uint32_t bitmap;
        };

        struct CopyOnWrite
        {
            BlockOffset offset;
        };

        std::optional<Overlay> m_overlay;
        std::optional<CopyOnWrite> m_copyOnWrite;
    };

    static void Parse(StreamReader& stream, const GUID& shadowCopyGuid, ShadowCopy& shadowCopy, std::error_code& ec);

    static void Parse(StreamReader& stream, std::vector<ShadowCopy>& shadowCopies, std::error_code& ec);

    size_t ReadAt(StreamReader& stream, uint64_t offset, BufferSpan output, std::error_code& ec) const;

    void GetBlockDescriptors(
        BlockOffset offset,
        std::optional<Block::Overlay>& overlay,
        std::optional<Block::CopyOnWrite>& copyOnWrite) const;

    const std::vector<uint8_t>& Bitmap() const { return m_bitmap; }

    ShadowCopyInformation& Information() { return m_information; }
    const ShadowCopyInformation& Information() const { return m_information; }

    const std::unordered_map<BlockOffset, Block>& Blocks() const { return m_blocks; }

private:
    // Initialize a shadow copy instance using the first snapshot id as shadow copy id. The next snapshots being newer
    // and ordered to the newest.
    static void Initialize(gsl::span<const Snapshot> snapshots, ShadowCopy& shadowCopy, std::error_code& ec);

private:
    ShadowCopyInformation m_information;
    std::vector<uint8_t> m_bitmap;
    std::unordered_map<BlockOffset, Block> m_blocks;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
