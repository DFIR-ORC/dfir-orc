//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Snapshot.h"

#include "Filesystem/Ntfs/ShadowCopy/SnapshotsIndex.h"
#include "Filesystem/Ntfs/ShadowCopy/DiffAreaTable.h"
#include "Filesystem/Ntfs/ShadowCopy/DiffAreaLocationTable.h"
#include "Filesystem/Ntfs/ShadowCopy/DiffAreaBitmap.h"
#include "Stream/StreamUtils.h"
#include "Text/Fmt/GUID.h"
#include "Text/Fmt/FILETIME.h"

using namespace Orc::Ntfs::ShadowCopy;
using namespace Orc::Ntfs;
using namespace Orc;

namespace {

void ParseOverlayEntries(
    const std::vector<DiffAreaTableEntry>& diffAreaEntries,
    std::unordered_map<Snapshot::BlockOffset, Snapshot::Overlay>& overlays)
{
    using Overlay = Snapshot::Overlay;

    for (const auto& entry : diffAreaEntries)
    {
        if (entry.DataRelativeOffset() != 1)
        {
            overlays.emplace(entry.Offset(), Overlay {entry.DataOffset(), entry.Bitmap()});
            continue;
        }

        auto it = overlays.find(entry.Offset());
        if (it == std::end(overlays))
        {
            Log::Debug("Missing overlay entry (offset: {:#x}, data: {:#x})", entry.Offset(), entry.DataOffset());
            continue;
        }

        auto& overlay = it->second;
        if (entry.DataOffset() != overlay.offset)
        {
            Log::Debug(
                "Mismatching data offset for existing overlay (offset: {:#x}, data #1: {:#x}, data #2: {:#x})",
                entry.Offset(),
                entry.DataOffset(),
                overlay.offset);
            continue;
        }

        overlay.bitmap |= entry.Bitmap();
    }
}

void ParseCopyOnWriteEntries(
    const std::vector<DiffAreaTableEntry>& diffAreaEntries,
    std::unordered_map<Snapshot::BlockOffset, Snapshot::CopyOnWrite>& copyOnWrites,
    std::unordered_map<Snapshot::BlockOffset, Snapshot::Forwarder>& forwarders)
{
    using BlockOffset = Snapshot::BlockOffset;
    using CopyOnWrite = Snapshot::CopyOnWrite;

    for (const auto& entry : diffAreaEntries)
    {
        struct Cow
        {
            uint64_t offset;
            uint64_t dataOffset;
        };

        Cow cow;
        cow.offset = entry.Offset();
        if (HasFlag(entry.Flags(), DiffAreaTableEntryFlags::kForwarder))
        {
            cow.dataOffset = entry.DataRelativeOffset();
        }
        else
        {
            cow.dataOffset = entry.DataOffset();
        }

        auto forwarderIt = forwarders.find(cow.offset);
        if (forwarderIt != std::cend(forwarders))
        {
            cow.offset = forwarderIt->second.offset;
            forwarders.erase(forwarderIt);
        }

        copyOnWrites[cow.offset] =
            CopyOnWrite {cow.dataOffset, HasFlag(entry.Flags(), DiffAreaTableEntryFlags::kForwarder)};

        if (HasFlag(entry.Flags(), DiffAreaTableEntryFlags::kForwarder))
        {
            if (cow.offset != cow.dataOffset)
            {
                forwarders[cow.dataOffset] = Snapshot::Forwarder {cow.offset};
            }
            else
            {
                copyOnWrites.erase(cow.offset);
            }
        }
    }
}

}  // namespace

void Snapshot::Parse(StreamReader& stream, std::vector<Snapshot>& snapshots, std::error_code& ec)
{
    SnapshotsIndex snapshotsIndex;
    SnapshotsIndex::Parse(stream, snapshotsIndex, ec);
    if (ec)
    {
        Log::Debug("Failed to read vss index [{}]", ec);
        return;
    }

    if (snapshotsIndex.Items().empty())
    {
        Log::Debug("No snapshots found");
        return;
    }

    snapshots.resize(snapshotsIndex.Items().size());

    for (size_t i = 0; i < snapshotsIndex.Items().size(); ++i)
    {
        const auto& snapshotInformation = snapshotsIndex.Items()[i];

        Snapshot::Parse(stream, snapshotInformation, snapshots[i], ec);
        if (ec)
        {
            Log::Debug("Failed to parse vss snapshots: {} [{}]", snapshotInformation.ShadowCopyId(), ec);
            return;
        }
    }
}

void Snapshot::Parse(
    StreamReader& stream,
    const SnapshotInformation& snapshotInformation,
    Snapshot& snapshot,
    std::error_code& ec)
{
    const auto& diffAreaInfo = snapshotInformation.DiffAreaInfo();

    DiffAreaTable diffAreaTable;
    DiffAreaTable::Parse(stream, diffAreaInfo.FirstDiffAreaTableOffset(), diffAreaTable, ec);
    if (ec)
    {
        Log::Error("Failed to parse DiffAreaTable [{}]", ec);
        return;
    }

    DiffAreaLocationTable diffAreaLocationTable;
    DiffAreaLocationTable::Parse(stream, diffAreaInfo.FirstDiffLocationTableOffset(), diffAreaLocationTable, ec);
    if (ec)
    {
        Log::Error("Failed to parse DiffAreaTable [{}]", ec);
        return;
    }

    if (!diffAreaLocationTable.Items().empty())
    {
        Log::Critical(
            "Found unsupported DiffAreaLocationTable items, please provide a full disk dump to the devs (count: {})",
            diffAreaLocationTable.Items().size());
    }

    DiffAreaBitmap diffAreaBitmap;
    DiffAreaBitmap::Parse(stream, diffAreaInfo.FirstBitmapOffset(), diffAreaBitmap, ec);
    if (ec)
    {
        Log::Error("Failed to parse DiffAreaBitmap [{}]", ec);
        return;
    }

    DiffAreaBitmap previousDiffAreaBitmap;
    DiffAreaBitmap::Parse(stream, diffAreaInfo.PreviousBitmapOffset(), previousDiffAreaBitmap, ec);
    if (ec)
    {
        Log::Error("Failed to parse previous DiffAreaBitmap [{}]", ec);
        return;
    }

    snapshot.m_information = snapshotInformation;
    snapshot.m_bitmap = std::move(diffAreaBitmap.Bitmap());
    snapshot.m_previousBitmap = std::move(previousDiffAreaBitmap.Bitmap());

    ::ParseOverlayEntries(diffAreaTable.OverlayEntries(), snapshot.m_overlays);
    ::ParseCopyOnWriteEntries(diffAreaTable.CowEntries(), snapshot.m_copyOnWrites, snapshot.m_forwarders);
}

void Snapshot::ResolveForwarders(gsl::span<Snapshot> snapshots)
{
    for (size_t i = 0; i < snapshots.size() - 1; ++i)
    {
        auto& activeSnapshot = snapshots[i];
        auto newerSnapshots = snapshots.subspan(i + 1);

        ResolveForwarders(newerSnapshots, activeSnapshot);
    }
}

void Snapshot::ResolveForwarders(gsl::span<const Snapshot> newerSnapshots, Snapshot& snapshot)
{
    uint64_t unresolvedForwarder = 0;

    for (const auto& [offset, cow] : snapshot.m_copyOnWrites)
    {
        if (!cow.forward)
        {
            continue;
        }

        uint64_t offsetToFind = cow.offset;

        std::optional<Snapshot::CopyOnWrite> lastFoundCow;
        for (auto& newerSnapshot : newerSnapshots)
        {
            auto cowIt = newerSnapshot.CopyOnWrites().find(offsetToFind);
            if (cowIt == std::cend(newerSnapshot.CopyOnWrites()))
            {
                continue;
            }

            lastFoundCow = cowIt->second;
            if (!lastFoundCow->forward)
            {
                break;
            }

            offsetToFind = lastFoundCow->offset;
        }

        if (lastFoundCow)
        {
            if (lastFoundCow->forward)
            {
                ++unresolvedForwarder;
            }

            snapshot.m_copyOnWrites[offset] = std::move(*lastFoundCow);
        }
        else
        {
            ++unresolvedForwarder;
        }
    }

    if (unresolvedForwarder)
    {
        Log::Debug(
            "Unresolved VSS forwarder (shadow copy: {}, count: {})",
            snapshot.Information().ShadowCopyId(),
            unresolvedForwarder);
    }
}

Snapshot::Snapshot() {}
