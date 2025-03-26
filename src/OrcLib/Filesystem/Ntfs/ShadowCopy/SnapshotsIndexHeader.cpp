//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "SnapshotsIndexHeader.h"
#include "Stream/StreamUtils.h"
#include "Log/Log.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

void SnapshotsIndexHeader::Parse(StreamReader& stream, SnapshotsIndexHeader& header, std::error_code& ec)
{
    using namespace Orc;

    std::array<uint8_t, sizeof(SnapshotsIndexHeader::Layout)> buffer;
    ReadChunkAt(stream, SnapshotsIndexHeader::kVolumeOffset, buffer, ec);
    if (ec)
    {
        Log::Debug("Failed to read VSS header [{}]", ec);
        return;
    }

    SnapshotsIndexHeader::Parse(buffer, header, ec);
}

void SnapshotsIndexHeader::Parse(BufferView buffer, SnapshotsIndexHeader& header, std::error_code& ec)
{
    if (buffer.size() < sizeof(SnapshotsIndexHeader::Layout))
    {
        Log::Debug("Failed to parse VSS root header: invalid size");
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    auto& layout = *reinterpret_cast<const SnapshotsIndexHeader::Layout*>(buffer.data());
    Node::Parse(layout.node, header.m_node, ec);
    if (ec)
    {
        Log::Debug("VSS root header is corrupted: failed to parse node");
        return;
    }

    Log::Debug("VSS root header version: {}", header.m_node.Version());

    if (header.m_node.Version() > 2)
    {
        Log::Debug("VSS root header could be corrupted: unsupported version ({:#x})", header.m_node.Version());
        // return  // Do not return
    }

    if (header.m_node.Type() != NodeType::kVolumeHeader)
    {
        Log::Debug(
            "VSS root header is corrupted: unexpected node type (expected: 0x1, value: {:#x})",
            ToString(header.m_node.Type()));
        return;
    }

    header.m_firstCatalogOffset = *reinterpret_cast<const uint64_t*>(&layout.firstCatalogOffset);
    header.m_maximumSize = *reinterpret_cast<const uint64_t*>(&layout.maximumSize);
    header.m_volumeGuid = *reinterpret_cast<const GUID*>(&layout.volumeGuid);
    header.m_storageGuid = *reinterpret_cast<const GUID*>(&layout.storageGuid);
    header.m_flags = *reinterpret_cast<const uint64_t*>(&layout.flags);

    header.m_unknown1 = *reinterpret_cast<const uint64_t*>(&layout.unknown1);
    header.m_unknown2 = *reinterpret_cast<const uint64_t*>(&layout.unknown2);
    header.m_unknown3 = *reinterpret_cast<const uint32_t*>(&layout.unknown3);
    header.m_protectionFlags = *reinterpret_cast<const uint32_t*>(&layout.protectionFlags);

    std::string_view padding(reinterpret_cast<const char*>(layout.padding), sizeof(layout.padding));
    if (!std::all_of(std::cbegin(padding), std::cend(padding), [](char c) { return c == 0; }))
    {
        Log::Debug("VSS root header has unexpected non-zero padding");
    }
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
