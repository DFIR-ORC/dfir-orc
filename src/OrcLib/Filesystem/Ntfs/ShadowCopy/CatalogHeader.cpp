//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "CatalogHeader.h"

#include "Filesystem/Ntfs/ShadowCopy/Catalog.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

void CatalogHeader::Parse(BufferView buffer, CatalogHeader& header, std::error_code& ec)
{
    if (buffer.size() < sizeof(CatalogHeader::Layout))
    {
        Log::Debug("Failed to parse VSS catalog header: invalid size");
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    auto& layout = *reinterpret_cast<const CatalogHeader::Layout*>(buffer.data());
    Node::Parse(layout.node, header.m_node, ec);
    if (ec)
    {
        Log::Debug("VSS catalog header is corrupted: failed to parse node");
        return;
    }

    if (header.m_node.Type() != NodeType::kCatalog)
    {
        Log::Debug(
            "VSS catalog header could be corrupted: unexpected node type (expected: 2, value: {:#x})",
            std::underlying_type_t<NodeType>(header.m_node.Type()));
        ec = std::make_error_code(std::errc::invalid_argument);
        return;
    }

    if (header.m_node.Next() && header.m_node.Offset() + Catalog::kBlockSize != header.m_node.Next())
    {
        Log::Debug("VSS catalog header could be corrupted: unexpected 'nextCatalogOffset' value");
        return;
    }

    std::string_view padding(reinterpret_cast<const char*>(layout.padding), sizeof(layout.padding));
    if (!std::all_of(std::cbegin(padding), std::cend(padding), [](char c) { return c == 0; }))
    {
        Log::Debug("VSS catalog header has unexpected non-zero padding");
    }
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
