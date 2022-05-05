//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <unordered_map>

#include "Filesystem/Ntfs/ShadowCopy/SnapshotInformation.h"
#include "Stream/StreamReader.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class Snapshot final
{
public:
    using BlockOffset = uint64_t;

#pragma pack(push, 1)
    struct Overlay
    {
        BlockOffset offset;
        uint32_t bitmap;
    };

    struct CopyOnWrite
    {
        BlockOffset offset;
        bool forward;
    };

    struct Forwarder
    {
        BlockOffset offset;
    };
#pragma pack(pop)

    static void Parse(StreamReader& stream, std::vector<Snapshot>& snapshots, std::error_code& ec);

    static void Parse(
        StreamReader& stream,
        const SnapshotInformation& snapshotInformation,
        Snapshot& snapshot,
        std::error_code& ec);

    Snapshot();

    const std::vector<uint8_t>& Bitmap() const { return m_bitmap; }
    const std::vector<uint8_t>& PreviousBitmap() const { return m_previousBitmap; }

    const std::unordered_map<BlockOffset, Overlay>& Overlays() const { return m_overlays; }
    const std::unordered_map<BlockOffset, CopyOnWrite>& CopyOnWrites() const { return m_copyOnWrites; }
    const std::unordered_map<BlockOffset, Forwarder>& Forwarders() const { return m_forwarders; }

    const SnapshotInformation& Information() const { return m_information; }

    // Resolve the forwarders and update the copy-on-writes of the provided snapshot using the forwarders of the next
    // newer snapshots. Must be ordered to the newest.
    static void ResolveForwarders(gsl::span<const Snapshot> snapshots, Snapshot& newerSnapshots);

    // Resolve the forwarders and update the copy-on-writes of all the provided snapshots. Must be ordered to the
    // newest.
    static void ResolveForwarders(gsl::span<Snapshot> snapshots);

private:
    SnapshotInformation m_information;
    std::vector<uint8_t> m_bitmap;
    std::vector<uint8_t> m_previousBitmap;
    std::unordered_map<BlockOffset, Overlay> m_overlays;
    std::unordered_map<BlockOffset, CopyOnWrite> m_copyOnWrites;
    std::unordered_map<BlockOffset, Forwarder> m_forwarders;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
