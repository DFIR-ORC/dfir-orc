//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "CatalogEntry.h"

#include "Filesystem/Ntfs/ShadowCopy/CatalogEntryType.h"
#include "Filesystem/Ntfs/ShadowCopy/Catalog.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

void CatalogEntry::EndOfCatalog::Parse(BufferView buffer, CatalogEntry::EndOfCatalog& entry, std::error_code& ec)
{
    if (buffer.size() < sizeof(EndOfCatalog::Layout))
    {
        Log::Debug("Failed to parse VSS catalog 'EndOfCatalog' entry: invalid size");
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    auto& layout = *reinterpret_cast<const EndOfCatalog::Layout*>(buffer.data());

    const auto type = *reinterpret_cast<const uint32_t*>(&layout.type);
    if (static_cast<CatalogEntryType>(type) != CatalogEntryType::kCatalogEnd)
    {
        Log::Debug("Failed to parse VSS catalog 'EndOfCatalog' entry: invalid type (value: {})", type);
        return;
    }

    std::string_view padding(reinterpret_cast<const char*>(layout.padding), sizeof(layout.padding));
    if (!std::all_of(std::cbegin(padding), std::cend(padding), [](char c) { return c == 0; }))
    {
        Log::Debug("VSS catalog 'EndOfCatalog' entry has unexpected non-zero padding");
    }
}

void CatalogEntry::FreeEntry::Parse(BufferView buffer, FreeEntry& entry, std::error_code& ec)
{
    if (buffer.size() < sizeof(FreeEntry::Layout))
    {
        Log::Debug("Failed to parse VSS catalog 'FreeEntry': invalid size");
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    auto& layout = *reinterpret_cast<const FreeEntry::Layout*>(buffer.data());

    const auto type = *reinterpret_cast<const uint32_t*>(&layout.type);
    if (static_cast<CatalogEntryType>(type) != CatalogEntryType::kFreeEntry)
    {
        Log::Debug("Failed to parse VSS catalog 'FreeEntry': invalid type (value: {})", type);
        return;
    }

    std::string_view padding(reinterpret_cast<const char*>(layout.padding), sizeof(layout.padding));
    if (!std::all_of(std::cbegin(padding), std::cend(padding), [](char c) { return c == 0; }))
    {
        Log::Debug("VSS catalog 'FreeEntry' header has unexpected non-zero padding");
    }
}

void CatalogEntry::SnapshotInfo::Parse(BufferView buffer, SnapshotInfo& entry, std::error_code& ec)
{
    if (buffer.size() < sizeof(SnapshotInfo::Layout))
    {
        Log::Debug("Failed to parse VSS catalog entry 'SnapshotInfo': invalid size");
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    auto& layout = *reinterpret_cast<const SnapshotInfo::Layout*>(buffer.data());

    const auto type = *reinterpret_cast<const uint32_t*>(&layout.type);
    if (static_cast<CatalogEntryType>(type) != CatalogEntryType::kSnapshotInfo)
    {
        Log::Debug("Failed to parse VSS catalog entry 'SnapshotInfo': invalid type (value: {})", type);
        ec = std::make_error_code(std::errc::bad_message);
        return;
    }

    {
        const auto size = *reinterpret_cast<const int64_t*>(&layout.size);
        if (size <= 0)
        {
            Log::Debug("Failed to parse VSS catalog entry 'SnapshotInfo': invalid size field (value: {})", size);
            ec = std::make_error_code(std::errc::bad_message);
            return;
        }

        entry.m_size = size;
    }

    entry.m_guid = *reinterpret_cast<const GUID*>(&layout.guid);

    {
        const auto position = *reinterpret_cast<const int64_t*>(&layout.position);
        if (position < 0)
        {
            Log::Debug(
                "Failed to parse VSS catalog entry 'SnapshotInfo': invalid position field (value: {})", position);
            ec = std::make_error_code(std::errc::bad_message);
            return;
        }

        entry.m_position = position;
    }

    entry.m_flags = *reinterpret_cast<const uint64_t*>(&layout.flags);

    entry.m_creationTime = *reinterpret_cast<const FILETIME*>(&layout.creationTime);

    Log::Debug(
        "VSS catalog entry 'SnapshotInfo::unknown56' = {}", *reinterpret_cast<const uint16_t*>(&layout.unknown56));

    std::string_view padding(reinterpret_cast<const char*>(layout.padding), sizeof(layout.padding));
    if (!std::all_of(std::cbegin(padding), std::cend(padding), [](char c) { return c == 0; }))
    {
        Log::Debug("VSS catalog entry 'SnapshotInfo' has unexpected non-zero padding");
    }
}

void CatalogEntry::DiffAreaInfo::Parse(BufferView buffer, DiffAreaInfo& snapshot, std::error_code& ec)
{
    if (buffer.size() < sizeof(DiffAreaInfo::Layout))
    {
        Log::Debug("Failed to parse VSS catalog 'DiffAreaInfo': invalid size");
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    auto& layout = *reinterpret_cast<const DiffAreaInfo::Layout*>(buffer.data());

    const auto type = *reinterpret_cast<const uint32_t*>(&layout.type);
    if (static_cast<CatalogEntryType>(type) != CatalogEntryType::kDiffAreaInfo)
    {
        Log::Debug("Failed to parse VSS catalog 'DiffAreaInfo': invalid type (value: {})", type);
        return;
    }

    {
        const auto offset = *reinterpret_cast<const int64_t*>(&layout.firstDiffTableOffset);
        if (offset <= 0 || offset & (Catalog::kBlockSize - 1))
        {
            Log::Debug("Failed to parse VSS catalog 'DiffAreaInfo': invalid 'diffTableOffset' (value: {})", offset);
            ec = std::make_error_code(std::errc::bad_message);
            return;
        }

        snapshot.m_diffAreaTableOffset = offset;
    }

    snapshot.m_guid = *reinterpret_cast<const GUID*>(&layout.guid);

    {
        const auto offset = *reinterpret_cast<const int64_t*>(&layout.applicationInfoOffset);
        if (offset <= 0 || offset & (Catalog::kBlockSize - 1))
        {
            Log::Debug(
                "Failed to parse VSS catalog 'DiffAreaInfo': invalid 'applicationInfoOffset' (value: {})", offset);
            ec = std::make_error_code(std::errc::bad_message);
            return;
        }

        snapshot.m_applicationInfoOffset = offset;
    }

    {
        const auto offset = *reinterpret_cast<const int64_t*>(&layout.diffLocationTableOffset);
        if (offset <= 0 || offset & (Catalog::kBlockSize - 1))
        {
            Log::Debug(
                "Failed to parse VSS catalog 'DiffAreaInfo': invalid 'diffLocationTableOffset' (value: {})", offset);
            ec = std::make_error_code(std::errc::bad_message);
            return;
        }
    }

    {
        const auto offset = *reinterpret_cast<const int64_t*>(&layout.bitmapOffset);
        if (offset <= 0 || offset & (Catalog::kBlockSize - 1))
        {
            Log::Debug("Failed to parse VSS catalog 'DiffAreaInfo': invalid 'bitmapOffset' (value: {})", offset);
            ec = std::make_error_code(std::errc::bad_message);
            return;
        }

        snapshot.m_bitmapOffset = offset;
    }

    snapshot.m_frn = *reinterpret_cast<const uint64_t*>(&layout.frn);
    snapshot.m_allocatedSize = *reinterpret_cast<const uint64_t*>(&layout.allocatedSize);
    snapshot.m_previousBitmapOffset = *reinterpret_cast<const int64_t*>(&layout.previousBitmapOffset);

    std::string_view padding(reinterpret_cast<const char*>(layout.padding), sizeof(layout.padding));
    if (!std::all_of(std::cbegin(padding), std::cend(padding), [](char c) { return c == 0; }))
    {
        Log::Debug("VSS catalog entry 'DiffAreaInfo' has unexpected non-zero padding");
    }
}

void CatalogEntry::Parse(BufferView buffer, CatalogEntry& entry, std::error_code& ec)
{
    if (buffer.size() < sizeof(uint64_t))
    {
        Log::Debug("Failed to parse VSS catalog entry header: invalid size");
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    const auto type = *reinterpret_cast<const CatalogEntryType*>(buffer.data());
    switch (type)
    {
        case CatalogEntryType::kCatalogEnd:
            entry.m_value = EndOfCatalog();
            EndOfCatalog::Parse(buffer, *std::get_if<EndOfCatalog>(&entry.m_value), ec);
            break;
        case CatalogEntryType::kFreeEntry:
            entry.m_value = FreeEntry();
            FreeEntry::Parse(buffer, *std::get_if<FreeEntry>(&entry.m_value), ec);
            break;
        case CatalogEntryType::kSnapshotInfo:
            entry.m_value = SnapshotInfo();
            SnapshotInfo::Parse(buffer, *std::get_if<SnapshotInfo>(&entry.m_value), ec);
            break;
        case CatalogEntryType::kDiffAreaInfo:
            entry.m_value = DiffAreaInfo();
            DiffAreaInfo::Parse(buffer, *std::get_if<DiffAreaInfo>(&entry.m_value), ec);
            break;
        default:
            ec = std::make_error_code(std::errc::invalid_argument);
            return;
    }
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
