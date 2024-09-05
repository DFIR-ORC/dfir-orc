//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <cstdint>

#include "Utils/BufferView.h"
#include "Filesystem/Ntfs/ShadowCopy/DiffAreaTableEntryFlags.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class DiffAreaTableEntry final
{
public:
    static constexpr size_t kDataSize = 16384;

#pragma pack(push, 1)
    struct Layout
    {
        uint8_t offset[8];
        uint8_t dataRelativeOffset[8];
        uint8_t dataOffset[8];
        uint8_t flags[4];
        uint8_t bitmap[4];
    };
#pragma pack(pop)

    static void Parse(BufferView buffer, DiffAreaTableEntry& entry, std::error_code& ec);

    uint64_t Offset() const { return m_offset; }
    uint64_t DataOffset() const { return m_dataOffset; }
    uint64_t DataRelativeOffset() const { return m_dataRelativeOffset; }
    uint32_t Bitmap() const { return m_bitmap; }
    DiffAreaTableEntryFlags Flags() const { return m_flags; }

private:
    uint64_t m_offset;
    uint64_t m_dataRelativeOffset;
    uint64_t m_dataOffset;
    DiffAreaTableEntryFlags m_flags;
    uint32_t m_bitmap;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
