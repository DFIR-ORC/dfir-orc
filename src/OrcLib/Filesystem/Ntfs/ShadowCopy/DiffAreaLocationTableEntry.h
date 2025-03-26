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

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class DiffAreaLocationTableEntry final
{
public:
#pragma pack(push, 1)
    struct Layout
    {
        uint8_t offset[8];
        uint8_t dataRelativeOffset[8];
        uint8_t dataSize[8];
    };
#pragma pack(pop)

    static void Parse(BufferView buffer, DiffAreaLocationTableEntry& entry, std::error_code& ec);

    uint64_t Offset() const { return m_offset; }
    uint64_t DataRelativeOffset() const { return m_dataRelativeOffset; }
    uint64_t Size() const { return m_dataSize; }

private:
    uint64_t m_offset;
    uint64_t m_dataRelativeOffset;
    uint64_t m_dataSize;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
