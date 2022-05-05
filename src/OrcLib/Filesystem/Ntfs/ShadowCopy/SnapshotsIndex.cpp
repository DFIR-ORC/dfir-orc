//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "SnapshotsIndex.h"

#include <windows.h>

#include "Filesystem/Ntfs/ShadowCopy/ApplicationInformation.h"
#include "Filesystem/Ntfs/ShadowCopy/Catalog.h"
#include "Filesystem/Ntfs/ShadowCopy/DiffAreaTableHeader.h"
#include "Filesystem/Ntfs/ShadowCopy/DiffAreaTableEntry.h"
#include "Filesystem/Ntfs/ShadowCopy/DiffAreaBitmap.h"
#include "Filesystem/Ntfs/ShadowCopy/DiffAreaLocationTable.h"
#include "Filesystem/Ntfs/ShadowCopy/SnapshotsIndexHeader.h"

#include "Log/Log.h"
#include "Text/Fmt/GUID.h"
#include "Text/Fmt/FILETIME.h"
#include "Stream/StreamUtils.h"

using namespace Orc::Ntfs::ShadowCopy;
using namespace Orc::Ntfs;
using namespace Orc;

namespace {

void ReadSnapshotsInformation(
    StreamReader& stream,
    const Catalog& catalog,
    std::vector<SnapshotInformation>& snapshots,
    std::error_code& ec)
{
    using SnapshotInfo = CatalogEntry::SnapshotInfo;
    using DiffAreaInfo = CatalogEntry::DiffAreaInfo;

    std::vector<const SnapshotInfo*> snapshotInfoItems;
    std::vector<const DiffAreaInfo*> diffAreaInfoItems;

    for (const auto& entry : catalog.Entries())
    {
        auto snapshotInfo = entry.Get<SnapshotInfo>();
        if (snapshotInfo)
        {
            snapshotInfoItems.push_back(snapshotInfo);
            continue;
        }

        auto diffAreaInfo = entry.Get<DiffAreaInfo>();
        if (diffAreaInfo)
        {
            diffAreaInfoItems.push_back(diffAreaInfo);
        }
    }

    if (snapshotInfoItems.empty())
    {
        Log::Debug("Failed to process ShadowCopy root index: missing SnapshotInfo");
        ec = std::make_error_code(std::errc::bad_message);
        return;
    }

    if (diffAreaInfoItems.empty())
    {
        Log::Debug("Failed to process ShadowCopy root index: missing DiffAreaInfo");
        ec = std::make_error_code(std::errc::bad_message);
        return;
    }

    if (snapshotInfoItems.size() != diffAreaInfoItems.size())
    {
        Log::Debug("Failed to process ShadowCopy root index: SnapshotInfo/DiffAreaInfo item count mismatch");
        ec = std::make_error_code(std::errc::bad_message);
        return;
    }

    for (const auto* snapshotInfo : snapshotInfoItems)
    {
        auto diffAreaInfoIt =
            std::find_if(std::cbegin(diffAreaInfoItems), std::cend(diffAreaInfoItems), [&](const DiffAreaInfo* item) {
                return snapshotInfo->Guid() == item->Guid();
            });

        if (diffAreaInfoIt == std::cend(diffAreaInfoItems))
        {
            Log::Debug(
                "Failed to process ShadowCopy root index: missing DiffAreaInfo (guid: {})", snapshotInfo->Guid());
            ec = std::make_error_code(std::errc::bad_message);
            return;
        }

        std::array<uint8_t, ApplicationInformation::kBlockSize> applicationInfoBuffer;
        ReadChunkAt(stream, (*diffAreaInfoIt)->ApplicationInfoOffset(), applicationInfoBuffer, ec);
        if (ec)
        {
            Log::Debug("Failed to process ShadowCopy root index: failed to read ApplicationInformation [{}]", ec);
            return;
        }

        ApplicationInformation applicationInfo;
        ApplicationInformation::Parse(applicationInfoBuffer, applicationInfo, ec);
        if (ec)
        {
            Log::Debug("Failed to process ShadowCopy root index: failed to parse ApplicationInformation [{}]", ec);
            return;
        }

        auto vssLocalInfo = applicationInfo.Get<ApplicationInformation::VssLocalInfo>();
        if (!vssLocalInfo)
        {
            Log::Debug("Failed to process ShadowCopy root index: no VssLocalInfo");
            ec = std::make_error_code(std::errc::not_supported);
            return;
        }

        snapshots.emplace_back(*snapshotInfo, **diffAreaInfoIt, std::move(*vssLocalInfo));
    }

    std::sort(std::begin(snapshots), std::end(snapshots), [](const auto& lhs, const auto& rhs) {
        return lhs.LayerPosition() < rhs.LayerPosition();
    });
}

}  // namespace

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

Result<const SnapshotInformation*> SnapshotsIndex::Get(const std::string& shadowCopyId) const
{
    std::error_code ec;
    GUID guid;
    ToGuid(std::string_view(shadowCopyId), guid, ec);
    if (ec)
    {
        return ec;
    }

    return Get(guid);
}

Result<const SnapshotInformation*> SnapshotsIndex::Get(const GUID& shadowCopyId) const
{
    auto it =
        std::find_if(std::cbegin(m_items), std::cend(m_items), [&shadowCopyId](const SnapshotInformation& snapshot) {
            return snapshot.ShadowCopyId() == shadowCopyId;
        });

    if (it == std::cend(m_items))
    {
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }

    return &*it;
}

void SnapshotsIndex::Parse(StreamReader& stream, SnapshotsIndex& snapshotsIndex, std::error_code& ec)
{
    SnapshotsIndexHeader header;
    SnapshotsIndexHeader::Parse(stream, header, ec);
    if (ec)
    {
        Log::Debug("Failed to parse VSS root index header [{}]", ec);
        return;
    }

    Catalog catalog;
    Catalog::Parse(stream, header.FirstCatalogOffset(), catalog, ec);
    if (ec)
    {
        Log::Debug("Failed to parse VSS Catalog [{}]", ec);
        return;
    }

    if (catalog.Entries().empty())
    {
        Log::Debug("No VSS catalog found");
        return;
    }

    ::ReadSnapshotsInformation(stream, catalog, snapshotsIndex.m_items, ec);
    if (ec)
    {
        Log::Error("Failed to parse one or more VSS catalog [{}]", ec);
        ec.clear();
    }

    if (snapshotsIndex.Items().empty())
    {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return;
    }
}

const std::vector<SnapshotInformation>& SnapshotsIndex::Items() const
{
    return m_items;
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
