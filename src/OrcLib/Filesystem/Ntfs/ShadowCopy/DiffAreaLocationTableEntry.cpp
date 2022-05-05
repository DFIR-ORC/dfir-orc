//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "DiffAreaLocationTableEntry.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

void DiffAreaLocationTableEntry::Parse(BufferView buffer, DiffAreaLocationTableEntry& entry, std::error_code& ec)
{
    if (buffer.size() < sizeof(DiffAreaLocationTableEntry::Layout))
    {
        Log::Debug("Failed to parse VSS store location entry: invalid size");
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    auto& layout = *reinterpret_cast<const DiffAreaLocationTableEntry::Layout*>(buffer.data());

    entry.m_offset = *reinterpret_cast<const uint64_t*>(&layout.offset);
    entry.m_dataRelativeOffset = *reinterpret_cast<const uint64_t*>(&layout.dataRelativeOffset);
    entry.m_dataSize = *reinterpret_cast<const int64_t*>(&layout.dataSize);
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
