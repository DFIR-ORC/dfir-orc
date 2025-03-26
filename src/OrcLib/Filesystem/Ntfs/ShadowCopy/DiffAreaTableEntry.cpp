//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "DiffAreaTableEntry.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

void DiffAreaTableEntry::Parse(BufferView buffer, DiffAreaTableEntry& entry, std::error_code& ec)
{
    if (buffer.size() < sizeof(DiffAreaTableEntry::Layout))
    {
        Log::Debug("Failed to parse VSS store entry: invalid size");
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    auto& layout = *reinterpret_cast<const DiffAreaTableEntry::Layout*>(buffer.data());

    entry.m_offset = *reinterpret_cast<const uint64_t*>(&layout.offset);
    entry.m_dataRelativeOffset = *reinterpret_cast<const uint64_t*>(&layout.dataRelativeOffset);
    entry.m_dataOffset = *reinterpret_cast<const uint64_t*>(&layout.dataOffset);

    const auto flags = *reinterpret_cast<const uint32_t*>(&layout.flags);
    entry.m_flags = ToDiffAreaEntryFlags(flags, ec);
    if (ec)
    {
        Log::Debug("Failed VSS store entry consistency check: unknown flag(s) in {:#x}", flags);
        ec.clear();
    }

    if (HasFlag(entry.m_flags, DiffAreaTableEntryFlags::kForwarder)
        && HasFlag(entry.m_flags, DiffAreaTableEntryFlags::kOverlay))
    {
        Log::Debug("Failed VSS store entry consistency check: unexpected flags in {:#x}", flags);
        ec.clear();
    }

    if (entry.m_offset % 0x4000 != 0 || entry.m_dataOffset % 0x4000 != 0)
    {
        Log::Warn("Failed DiffAreaTableEntry consistency check: unaligned offsets");
        ec = std::make_error_code(std::errc::bad_message);
        return;
    }

    entry.m_bitmap = *reinterpret_cast<const uint32_t*>(&layout.bitmap);
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
