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

#include "Filesystem/Ntfs/ShadowCopy/Node.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class CatalogHeader final
{
public:
#pragma pack(push, 1)
    struct Layout
    {
        Node::Layout node;
        uint8_t padding[128 - sizeof(Node::Layout)];
    };
#pragma pack(pop)

    static void Parse(BufferView buffer, CatalogHeader& header, std::error_code& ec);

    const uint64_t NextHeaderOffset() const { return m_node.Next(); }

private:
    Node m_node;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
