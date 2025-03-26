//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "DiffAreaTableHeader.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

void DiffAreaTableHeader::Parse(BufferView buffer, DiffAreaTableHeader& header, std::error_code& ec)
{
    if (buffer.size() < sizeof(DiffAreaTableHeader::Layout))
    {
        Log::Debug("Failed to parse VSS store header: invalid size");
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    auto& layout = *reinterpret_cast<const DiffAreaTableHeader::Layout*>(buffer.data());
    Node::Parse(layout.node, header.m_node, ec);
    if (ec)
    {
        Log::Debug("VSS store header is corrupted: failed to parse node");
        return;
    }

    if (header.m_node.Type() != NodeType::kDiffAreaTable)
    {
        Log::Debug("VSS store header could be corrupted: unexpected node type");
        ec = std::make_error_code(std::errc::bad_message);
        return;
    }

    std::string_view padding(reinterpret_cast<const char*>(layout.padding), sizeof(layout.padding));
    if (!std::all_of(std::cbegin(padding), std::cend(padding), [](char c) { return c == 0; }))
    {
        Log::Debug("VSS store header has unexpected non-zero padding");
    }
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
