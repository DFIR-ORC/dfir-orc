//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <cstdint>

#include "Filesystem/Ntfs/ShadowCopy/DiffAreaLocationTableEntry.h"
#include "Stream/StreamReader.h"
#include "Utils/BufferView.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class DiffAreaLocationTable final
{
public:
    const static uint16_t kBlockSize = 16384;

    static void Parse(
        StreamReader& stream,
        uint64_t diffAreaTableOffset,
        DiffAreaLocationTable& diffAreaLocationTable,
        std::error_code& ec);

    static void Parse(
        BufferView buffer,
        DiffAreaLocationTable& diffAreaLocationTable,
        uint64_t& nextTableOffset,
        std::error_code& ec);

    const std::vector<DiffAreaLocationTableEntry>& Items() const { return m_items; }

private:
    std::vector<DiffAreaLocationTableEntry> m_items;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
