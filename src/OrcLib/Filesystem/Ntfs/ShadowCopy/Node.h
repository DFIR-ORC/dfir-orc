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

#include "Filesystem/Ntfs/ShadowCopy/NodeType.h"
#include "Utils/BufferView.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

// See volsnap.sys: VspDiscoverSnapshots, VspCreateStartBlock, VspQueryDiffArea
class Node final
{
public:
    static const GUID kMicrosoftVssIdentifier;

#pragma pack(push, 1)
    struct Layout
    {
        uint8_t guid[16];
        uint8_t version[4];
        uint8_t type[4];
        uint8_t relativeOffset[8];
        uint8_t offset[8];
        uint8_t next[8];
    };
#pragma pack(pop)

    static void Parse(BufferView buffer, Node& node, std::error_code& ec);
    static void Parse(const Node::Layout& layout, Node& node, std::error_code& ec);

    const GUID& Guid() const { return m_guid; }
    uint32_t Version() const { return m_version; }
    NodeType Type() const { return m_type; }
    uint64_t RelativeOffset() const { return m_relativeOffset; }
    uint64_t Offset() const { return m_offset; }
    uint64_t Next() const { return m_next; }

    const bool IsLastNode() const { return m_next == 0; }

private:
    GUID m_guid;
    uint32_t m_version;
    NodeType m_type;
    uint64_t m_relativeOffset;
    uint64_t m_offset;
    uint64_t m_next;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
