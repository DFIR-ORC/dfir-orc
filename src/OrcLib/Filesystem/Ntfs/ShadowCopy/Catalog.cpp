//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Catalog.h"

#include <unordered_set>

#include "Filesystem/Ntfs/ShadowCopy/CatalogHeader.h"
#include "Stream/StreamUtils.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

void Catalog::Parse(StreamReader& stream, uint64_t catalogOffset, Catalog& catalog, std::error_code& ec)
{
    auto offset = catalogOffset;

    // Block offsets come from the image. Remembering the ones already walked stops a chain that loops
    // back on any earlier block (A -> B -> A), where comparing against the current offset alone only
    // ever caught A -> A. The blocks parsed so far are kept: a truncated chain still carries entries.
    std::unordered_set<uint64_t> visitedOffsets;

    while (offset)
    {
        if (!visitedOffsets.insert(offset).second)
        {
            Log::Error("Failed to parse next block from VSS Catalog: loop back to offset {:#x}", offset);
            break;
        }

        std::array<uint8_t, Catalog::kBlockSize> buffer;
        ReadChunkAt(stream, offset, buffer, ec);
        if (ec)
        {
            Log::Debug("Failed to read VSS Catalog block (offset: {:#x}) [{}]", offset, ec);
            return;
        }

        uint64_t nextOffset;
        Parse(buffer, catalog, nextOffset, ec);
        if (ec)
        {
            Log::Debug("Failed to parse VSS Catalog block (offset: {:#x}) [{}]", offset, ec);
            return;
        }

        offset = nextOffset;
    }
}

void Catalog::Parse(BufferView buffer, Catalog& catalog, uint64_t& nextBlockOffset, std::error_code& ec)
{
    if (buffer.size() < Catalog::kBlockSize)
    {
        Log::Debug("Failed to parse VSS catalog block: invalid size");
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    CatalogHeader header;
    CatalogHeader::Parse(buffer, header, ec);
    if (ec)
    {
        Log::Debug("Failed to read VSS catalog header [{}]", ec);
        return;
    }

    nextBlockOffset = header.NextHeaderOffset();

    const auto maxEntryCount = (buffer.size() - sizeof(CatalogHeader::Layout)) / CatalogEntry::kLayoutSize;
    auto entriesBuffer = buffer.subspan(sizeof(CatalogHeader::Layout));
    for (size_t i = 0; i < maxEntryCount; ++i)
    {
        CatalogEntry entry;
        CatalogEntry::Parse(entriesBuffer.subspan(i * CatalogEntry::kLayoutSize), entry, ec);
        if (ec)
        {
            Log::Debug("Failed to read VSS catalog entry #{} [{}]", i, ec);
            return;
        }

        if (entry.Get<CatalogEntry::EndOfCatalog>())
        {
            break;
        }

        if (entry.Get<CatalogEntry::FreeEntry>())
        {
            continue;
        }

        catalog.m_entries.push_back(std::move(entry));
    }
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
