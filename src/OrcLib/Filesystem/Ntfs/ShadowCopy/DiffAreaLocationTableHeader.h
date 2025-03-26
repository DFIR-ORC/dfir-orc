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

#include "Utils/BufferView.h"
#include "Filesystem/Ntfs/ShadowCopy/Node.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class DiffAreaLocationTableHeader final
{
public:
#pragma pack(push, 1)
    struct Layout
    {
        Node::Layout node;
        uint8_t padding[128 - sizeof(Node::Layout)];
    };
#pragma pack(pop)

    static void Parse(BufferView buffer, DiffAreaLocationTableHeader& header, std::error_code& ec);

    const uint64_t NextHeaderOffset() { return m_node.Next(); }
    bool IsLastHeader() const { return m_node.IsLastNode(); }

private:
    Orc::Ntfs::ShadowCopy::Node m_node;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
