//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "DiffAreaTable.h"

#include "Filesystem/Ntfs/ShadowCopy/DiffAreaTableHeader.h"
#include "Stream/StreamUtils.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

void DiffAreaTable::Parse(
    StreamReader& stream,
    uint64_t diffAreaTableOffset,
    DiffAreaTable& diffAreaTable,
    std::error_code& ec)
{
    auto offset = diffAreaTableOffset;

    while (offset)
    {
        std::array<uint8_t, DiffAreaTable::kBlockSize> buffer;
        ReadChunkAt(stream, offset, buffer, ec);
        if (ec)
        {
            Log::Debug("Failed to read VSS DiffAreaTable block (offset: {:#x}) [{}]", offset, ec);
            return;
        }

        uint64_t nextOffset;
        Parse(buffer, diffAreaTable, nextOffset, ec);
        if (ec)
        {
            Log::Debug("Failed to parse VSS DiffAreaTable block (offset: {:#x}) [{}]", offset, ec);
            return;
        }

        if (offset == nextOffset)
        {
            Log::Error("Failed to parse next block from VSS DiffAreaTable (offset: {:#x})", offset);
            break;
        }

        offset = nextOffset;
    }
}

void DiffAreaTable::Parse(
    BufferView buffer,
    DiffAreaTable& diffAreaTable,
    uint64_t& nextHeaderOffset,
    std::error_code& ec)
{
    DiffAreaTableHeader header;
    DiffAreaTableHeader::Parse(buffer, header, ec);
    if (ec)
    {
        Log::Debug("Failed to parse VSS DiffAreaTableHeader [{}]", ec);
        return;
    }

    nextHeaderOffset = header.NextHeaderOffset();

    const auto entryCount = (buffer.size() - sizeof(DiffAreaTableHeader::Layout)) / sizeof(DiffAreaTableEntry::Layout);

    auto entriesBuffer = buffer.subspan(sizeof(DiffAreaTableHeader::Layout));
    for (size_t i = 0; i < entryCount; ++i)
    {
        DiffAreaTableEntry entry;
        DiffAreaTableEntry::Parse(entriesBuffer.subspan(i * sizeof(DiffAreaTableEntry)), entry, ec);
        if (ec)
        {
            Log::Debug("Failed to read VSS DiffAreaTable entry #{} [{}]", i, ec);
            return;
        }

        // Uninitialized entry
        if (entry.Offset() == 0 && entry.DataOffset() == 0 && entry.Bitmap() == 0
            && entry.Flags() == DiffAreaTableEntryFlags(0) && entry.DataRelativeOffset() == 0)
        {
            continue;
        }

        if (HasFlag(entry.Flags(), DiffAreaTableEntryFlags::kUnused))
        {
            continue;
        }

        if (HasFlag(entry.Flags(), DiffAreaTableEntryFlags::kOverlay))
        {
            diffAreaTable.m_overlayEntries.push_back(std::move(entry));
        }
        else
        {
            diffAreaTable.m_cowEntries.push_back(std::move(entry));
        }
    }
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
