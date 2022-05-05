//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Node.h"

#include "Log/Log.h"
#include "Text/Fmt/Guid.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

// Symbol VSP_DIFF_AREA_FILE_GUID: {3808876b-c176-4e48-b7ae-04046e6cc752}
const GUID Node::kMicrosoftVssIdentifier = {0x3808876b, 0xc176, 0x4e48, 0xb7, 0xae, 0x04, 0x04, 0x6e, 0x6c, 0xc7, 0x52};

void Node::Parse(BufferView buffer, Node& Node, std::error_code& ec)
{
    if (buffer.size() < sizeof(Node::Layout))
    {
        Log::Debug("Failed to parse VSS Node: invalid size");
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    Parse(*reinterpret_cast<const Node::Layout*>(buffer.data()), Node, ec);
}

void Node::Parse(const Node::Layout& layout, Node& node, std::error_code& ec)
{
    node.m_guid = *reinterpret_cast<const GUID*>(&layout.guid);
    if (node.m_guid != kMicrosoftVssIdentifier)
    {
        if (node.m_guid == GUID {0})
        {
            Log::Debug("VSS Node is corrupted: invalid null 'guid' ({})", node.m_guid);
        }
        else
        {
            Log::Debug("VSS Node is corrupted: invalid 'guid' ({})", node.m_guid);
        }

        ec = std::make_error_code(std::errc::bad_message);
        return;
    }

    // Version 1: Windows Vista/Seven
    // Version 2: Windows 8 (scoped snapshot)
    node.m_version = *reinterpret_cast<const uint32_t*>(&layout.version);
    if (node.m_version == 2)
    {
        Log::Debug("[!] VSS might have scoped snapshot");
    }

    const auto type = *reinterpret_cast<const uint32_t*>(&layout.type);
    node.m_type = ToNodeType(type, ec);
    if (ec)
    {
        Log::Debug("VSS node is corrupted: invalid 'type' ({})", type);
        ec = std::make_error_code(std::errc::bad_message);
        return;
    }

    node.m_relativeOffset = *reinterpret_cast<const uint64_t*>(&layout.relativeOffset);
    node.m_offset = *reinterpret_cast<const uint64_t*>(&layout.offset);
    node.m_next = *reinterpret_cast<const uint64_t*>(&layout.next);

    if (node.m_next == node.m_offset)
    {
        Log::Debug("VSS node is corrupted: invalid offset 'next'");
        ec = std::make_error_code(std::errc::bad_message);
        return;
    }
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
