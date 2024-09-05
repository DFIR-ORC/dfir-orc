//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Catalog.h"

#include "Filesystem/Ntfs/ShadowCopy/CatalogHeader.h"
#include "Stream/StreamUtils.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

void Catalog::Parse(StreamReader& stream, uint64_t catalogOffset, Catalog& catalog, std::error_code& ec)
{
    auto offset = catalogOffset;

    while (offset)
    {
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

        if (offset == nextOffset)
        {
            Log::Error("Failed to parse next block from VSS Catalog (offset: {:#x})", offset);
            break;
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
