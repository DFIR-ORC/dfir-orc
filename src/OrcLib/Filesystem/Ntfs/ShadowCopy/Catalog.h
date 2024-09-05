//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <cstdint>
#include <memory>

#include "Filesystem/Ntfs/ShadowCopy/CatalogEntry.h"
#include "Stream/StreamReader.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class Catalog final
{
public:
    // Catalog are stored as an array which elements are CatalogHeader and multiple CatalogEntry.
    // This is the size of an element of the catalog array.
    static const uint16_t kBlockSize = 16384;

    static void Parse(StreamReader& stream, uint64_t catalogOffset, Catalog& catalog, std::error_code& ec);
    static void Parse(BufferView buffer, Catalog& catalog, uint64_t& nextBlockOffset, std::error_code& ec);

    const std::vector<CatalogEntry>& Entries() const { return m_entries; }

private:
    std::vector<CatalogEntry> m_entries;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
