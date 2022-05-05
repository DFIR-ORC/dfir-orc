//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <cstdint>
#include <memory>

#include "Utils/BufferView.h"
#include "Filesystem/Ntfs/ShadowCopy/Node.h"
#include "Stream/StreamReader.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class SnapshotsIndexHeader final
{
public:
    static const uint16_t kVolumeOffset = 0x1E00;
    static constexpr size_t kExpectedLayoutSize = 512;

#pragma pack(push, 1)
    struct Layout
    {
        Node::Layout node;
        uint8_t firstCatalogOffset[8];
        uint8_t maximumSize[8];
        uint8_t volumeGuid[16];
        uint8_t storageGuid[16];
        uint8_t flags[8];
        uint8_t unknown1[8];
        uint8_t unknown2[8];
        uint8_t unknown3[4];
        uint8_t protectionFlags[4];
        uint8_t padding[384];  // zero bytes padding to 512
    };
#pragma pack(pop)

    static_assert(sizeof(Layout) == kExpectedLayoutSize);

    static void Parse(StreamReader& stream, SnapshotsIndexHeader& header, std::error_code& ec);
    static void Parse(BufferView buffer, SnapshotsIndexHeader& header, std::error_code& ec);

    const uint64_t FirstCatalogOffset() const { return m_firstCatalogOffset; }
    const uint64_t MaximumSize() const { return m_maximumSize; }
    const GUID& VolumeGuid() const { return m_volumeGuid; }
    const GUID& StorageGuid() const { return m_storageGuid; }
    uint32_t Version() const { return m_node.Version(); }
    uint64_t Flags() const { return m_flags; }
    uint32_t ProtectionFlags() const { return m_protectionFlags; }

    bool HasCatalog() const { return m_firstCatalogOffset != 0; }
    bool IsLocalStorage() const { return m_volumeGuid == m_storageGuid; }

private:
    Node m_node;
    uint64_t m_firstCatalogOffset;
    uint64_t m_maximumSize;
    GUID m_volumeGuid;
    GUID m_storageGuid;
    uint64_t m_flags;
    uint32_t m_protectionFlags;
    uint64_t m_unknown1;
    uint64_t m_unknown2;
    uint32_t m_unknown3;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
