//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <cstdint>

#include "Filesystem/Ntfs/ShadowCopy/Node.h"
#include "Utils/BufferView.h"
#include "Stream/StreamReader.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class DiffAreaBitmap final
{
public:
    const static uint16_t kBlockSize = 16384;

#pragma pack(push, 1)
    struct Layout
    {
        struct Header
        {
            Node::Layout node;
            uint8_t padding[128 - sizeof(Node::Layout)];  // zero bytes padding to 128 bytes
        } header;

        uint8_t bitmap[kBlockSize - sizeof(Layout::Header)];
    };
#pragma pack(pop)

    static void
    Parse(StreamReader& stream, uint64_t diffAreaBitmapOffset, DiffAreaBitmap& diffAreaBitmap, std::error_code& ec);

    static void Parse(BufferView buffer, DiffAreaBitmap& bitmap, uint64_t& nextBitmapOffset, std::error_code& ec);

    DiffAreaBitmap()
        : m_node()
        , m_bitmap()
    {
    }

    const std::vector<uint8_t>& Bitmap() const { return m_bitmap; }
    std::vector<uint8_t>& Bitmap() { return m_bitmap; }

private:
    Node m_node;
    std::vector<uint8_t> m_bitmap;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
