//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "DiffAreaLocationTableHeader.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

void DiffAreaLocationTableHeader::Parse(BufferView buffer, DiffAreaLocationTableHeader& header, std::error_code& ec)
{
    if (buffer.size() < sizeof(DiffAreaLocationTableHeader::Layout))
    {
        Log::Debug("Failed to parse VSS DiffAreaLocationTableHeader: invalid size");
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    auto& layout = *reinterpret_cast<const DiffAreaLocationTableHeader::Layout*>(buffer.data());
    Node::Parse(layout.node, header.m_node, ec);
    if (ec)
    {
        Log::Debug("VSS DiffAreaLocationTableHeader is corrupted: failed to parse node");
        return;
    }

    if (header.m_node.Type() != NodeType::kDiffAreaLocationTable)
    {
        Log::Debug("VSS DiffAreaLocationTableHeader could be corrupted: unexpected node type");
        ec = std::make_error_code(std::errc::bad_message);
        return;
    }

    std::string_view padding(reinterpret_cast<const char*>(layout.padding), sizeof(layout.padding));
    if (!std::all_of(std::cbegin(padding), std::cend(padding), [](char c) { return c == 0; }))
    {
        Log::Debug("VSS DiffAreaLocationTableHeader has unexpected non-zero padding");
    }
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
