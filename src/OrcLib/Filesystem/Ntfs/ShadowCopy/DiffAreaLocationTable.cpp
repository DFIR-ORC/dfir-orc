//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "DiffAreaLocationTable.h"
#include "DiffAreaLocationTableHeader.h"
#include "Stream/StreamUtils.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

void DiffAreaLocationTable::Parse(
    StreamReader& stream,
    uint64_t diffAreaTableOffset,
    DiffAreaLocationTable& diffAreaLocationTable,
    std::error_code& ec)
{
    auto offset = diffAreaTableOffset;

    while (offset)
    {
        std::array<uint8_t, DiffAreaLocationTable::kBlockSize> buffer;
        ReadChunkAt(stream, offset, buffer, ec);
        if (ec)
        {
            Log::Debug("Failed to read VSS DiffAreaLocationTable block (offset: {:#x}) [{}]", offset, ec);
            return;
        }

        uint64_t nextOffset;
        Parse(buffer, diffAreaLocationTable, nextOffset, ec);
        if (ec)
        {
            Log::Debug("Failed to parse VSS DiffAreaLocationTable block (offset: {:#x}) [{}]", offset, ec);
            return;
        }

        if (offset == nextOffset)
        {
            Log::Error("Failed to parse next block from VSS DiffAreaLocationTable (offset: {:#x})");
            break;
        }

        offset = nextOffset;
    }
}

void DiffAreaLocationTable::Parse(
    BufferView buffer,
    DiffAreaLocationTable& diffAreaTable,
    uint64_t& nextTableOffset,
    std::error_code& ec)
{
    DiffAreaLocationTableHeader header;
    DiffAreaLocationTableHeader::Parse(buffer, header, ec);
    if (ec)
    {
        Log::Debug("Failed to parse VSS DiffAreaLocationTableHeader [{}]", ec);
        return;
    }

    nextTableOffset = header.NextHeaderOffset();

    const auto entryCount =
        (buffer.size() - sizeof(DiffAreaLocationTableHeader::Layout)) / sizeof(DiffAreaLocationTableHeader::Layout);

    auto entriesBuffer = buffer.subspan(sizeof(DiffAreaLocationTableHeader::Layout));
    for (size_t i = 0; i < entryCount; ++i)
    {
        DiffAreaLocationTableEntry entry;
        DiffAreaLocationTableEntry::Parse(entriesBuffer.subspan(i * sizeof(DiffAreaLocationTableEntry)), entry, ec);
        if (ec)
        {
            Log::Debug("Failed to read VSS DiffAreaLocationTableEntry entry #{} [{}]", i, ec);
            return;
        }

        // Uninitialized entry
        if (entry.Offset() == 0 && entry.DataRelativeOffset() == 0 && entry.Size() == 0)
        {
            continue;
        }

        diffAreaTable.m_items.push_back(entry);
    }
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
