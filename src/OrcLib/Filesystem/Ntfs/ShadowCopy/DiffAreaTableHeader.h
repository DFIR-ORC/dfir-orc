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

#include <windows.h>

#include "Utils/BufferView.h"
#include "Filesystem/Ntfs/ShadowCopy/Node.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

// TODO: Need some disk image to work on this
class DiffAreaTableHeader
{
public:
    const static uint16_t kBlockSize = 16384;

#pragma pack(push, 1)
    struct Layout
    {
        Node::Layout node;
        // uint8_t unknown4[8];
        // uint8_t padding[72];
        uint8_t padding[80];  // zero bytes padding to 128
    };
#pragma pack(pop)

    static void Parse(BufferView buffer, DiffAreaTableHeader& header, std::error_code& ec);

    const uint64_t NextHeaderOffset() { return m_node.Next(); }
    bool IsLastHeader() const { return m_node.IsLastNode(); }

private:
    Orc::Ntfs::ShadowCopy::Node m_node;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
