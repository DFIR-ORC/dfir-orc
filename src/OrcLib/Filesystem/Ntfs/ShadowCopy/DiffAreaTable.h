//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "Log/Log.h"

#include "Filesystem/Ntfs/ShadowCopy/DiffAreaTableEntry.h"
#include "Stream/StreamReader.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class DiffAreaTable final
{
public:
    const static uint16_t kBlockSize = 16384;

    static void
    Parse(StreamReader& stream, uint64_t diffAreaTableOffset, DiffAreaTable& diffAreaTable, std::error_code& ec);

    static void Parse(BufferView buffer, DiffAreaTable& diffAreaTable, uint64_t& nextTableOffset, std::error_code& ec);

    const std::vector<DiffAreaTableEntry>& OverlayEntries() const { return m_overlayEntries; }
    std::vector<DiffAreaTableEntry>& OverlayEntries() { return m_overlayEntries; }

    const std::vector<DiffAreaTableEntry>& CowEntries() const { return m_cowEntries; }
    std::vector<DiffAreaTableEntry>& CowEntries() { return m_cowEntries; }

private:
    std::vector<DiffAreaTableEntry> m_overlayEntries;
    std::vector<DiffAreaTableEntry> m_cowEntries;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
