//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "DiffAreaBitmap.h"

#include "Stream/StreamUtils.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

void DiffAreaBitmap::Parse(
    StreamReader& stream,
    uint64_t diffAreaBitmapOffset,
    DiffAreaBitmap& diffAreaBitmap,
    std::error_code& ec)
{
    auto offset = diffAreaBitmapOffset;
    while (offset)
    {
        std::array<uint8_t, DiffAreaBitmap::kBlockSize> buffer;
        ReadChunkAt(stream, offset, buffer, ec);
        if (ec)
        {
            Log::Debug("Failed to read VSS DiffAreaBitmap block (offset: {:#x}) [{}]", offset, ec);
            return;
        }

        uint64_t nextOffset;
        Parse(buffer, diffAreaBitmap, nextOffset, ec);
        if (ec)
        {
            Log::Debug("Failed to parse VSS DiffAreaBitmap block (offset: {:#x}) [{}]", offset, ec);
            return;
        }

        if (offset == nextOffset)
        {
            Log::Error("Failed to parse next block from VSS DiffAreaBitmap (offset: {:#x})", offset);
            break;
        }

        offset = nextOffset;
    }
}
void DiffAreaBitmap::Parse(
    BufferView buffer,
    DiffAreaBitmap& diffAreaBitmap,
    uint64_t& nextBitmapOffset,
    std::error_code& ec)
{
    if (buffer.size() < sizeof(DiffAreaBitmap::Layout))
    {
        Log::Debug("Failed to parse DiffAreaBitmap: invalid size");
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    auto& layout = *reinterpret_cast<const DiffAreaBitmap::Layout*>(buffer.data());
    Node::Parse(layout.header.node, diffAreaBitmap.m_node, ec);
    if (ec)
    {
        Log::Debug("Failed to parse DiffAreaBitmap: failed to parse node [{}]", ec);
        return;
    }

    nextBitmapOffset = diffAreaBitmap.m_node.Next();

    if (diffAreaBitmap.m_node.Type() != NodeType::kDiffAreaBitmap)
    {
        Log::Debug(
            "Failed to parse DiffAreaBitmap: unexpected node type (expected: {}, got: {:#x})",
            std::underlying_type_t<NodeType>(NodeType::kDiffAreaBitmap),
            std::underlying_type_t<NodeType>(diffAreaBitmap.m_node.Type()));

        ec = std::make_error_code(std::errc::invalid_argument);
        return;
    }

    std::copy(std::cbegin(layout.bitmap), std::cend(layout.bitmap), std::back_inserter(diffAreaBitmap.m_bitmap));
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
