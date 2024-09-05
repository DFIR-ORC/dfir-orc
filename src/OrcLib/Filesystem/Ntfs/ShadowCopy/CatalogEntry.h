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
#include <variant>

#include "Utils/BufferView.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class CatalogEntry final
{
public:
    static constexpr size_t kLayoutSize = 128;

    class EndOfCatalog
    {
    public:
#pragma pack(push, 1)
        struct Layout
        {
            uint8_t type[4];
            uint8_t padding[124];  // zero bytes padding to 128
        };
#pragma pack(pop)

        static_assert(sizeof(Layout) == kLayoutSize);

        static void Parse(BufferView buffer, EndOfCatalog& entry, std::error_code& ec);
    };

    class FreeEntry
    {
    public:
#pragma pack(push, 1)
        struct Layout
        {
            uint8_t type[4];
            uint8_t padding[124];  // zero bytes padding to 128
        };
#pragma pack(pop)

        static_assert(sizeof(Layout) == kLayoutSize);

        static void Parse(BufferView buffer, FreeEntry& entry, std::error_code& ec);
    };

    class SnapshotInfo
    {
    public:
#pragma pack(push, 1)
        struct Layout
        {
            uint8_t type[4];
            uint8_t unknown4;
            uint8_t unknown5[3];
            uint8_t size[8];
            uint8_t guid[16];
            uint8_t position[8];  // layer position in the snapshot stack
            uint8_t flags[8];  // vista/seven: 0x40, w8: 0x440, "when unset, data blocks marked in the bitmap (unused
                               // blocks) point to original data (instead of being filled with null bytes)."
            uint8_t creationTime[8];
            uint8_t unknown56[2];
            uint8_t padding[70];  // zero bytes padding to 128
        };
#pragma pack(pop)

        static_assert(sizeof(Layout) == kLayoutSize);

        static void Parse(BufferView buffer, SnapshotInfo& entry, std::error_code& ec);

        uint64_t Size() const { return m_size; }
        const GUID& Guid() const { return m_guid; }
        uint64_t Position() const { return m_position; }
        uint64_t Flags() const { return m_flags; }
        const FILETIME& CreationTime() const { return m_creationTime; }

    private:
        uint64_t m_size;
        GUID m_guid;
        uint64_t m_position;  // OPTIMISATION: could probably use 32 bits
        uint64_t m_flags;
        FILETIME m_creationTime;
    };

    class DiffAreaInfo
    {
    public:
#pragma pack(push, 1)
        struct Layout
        {
            uint8_t type[4];
            uint8_t unknown4[4];
            uint8_t firstDiffTableOffset[8];
            uint8_t guid[16];
            uint8_t applicationInfoOffset[8];
            uint8_t diffLocationTableOffset[8];
            uint8_t bitmapOffset[8];
            uint8_t frn[8];
            uint8_t allocatedSize[8];
            uint8_t previousBitmapOffset[8];
            uint8_t unknown72[4];
            uint8_t padding[44];  // zero bytes padding to 128
        };
#pragma pack(pop)

        static_assert(sizeof(Layout) == kLayoutSize);

        static void Parse(BufferView buffer, DiffAreaInfo& snapshot, std::error_code& ec);

        uint64_t FirstDiffAreaTableOffset() const { return m_diffAreaTableOffset; }
        const GUID& Guid() const { return m_guid; }
        uint64_t ApplicationInfoOffset() const { return m_applicationInfoOffset; }
        uint64_t FirstDiffLocationTableOffset() const { return m_diffLocationTableOffset; }
        uint64_t FirstBitmapOffset() const { return m_bitmapOffset; }
        uint64_t Frn() const { return m_frn; }
        uint64_t AllocatedSize() const { return m_allocatedSize; }
        uint64_t PreviousBitmapOffset() const { return m_previousBitmapOffset; }

    private:
        uint64_t m_diffAreaTableOffset;
        GUID m_guid;
        uint64_t m_applicationInfoOffset;
        uint64_t m_diffLocationTableOffset;
        uint64_t m_bitmapOffset;
        uint64_t m_frn;
        uint64_t m_allocatedSize;
        uint64_t m_previousBitmapOffset;
    };

    using EntryHolder = std::variant<std::monostate, EndOfCatalog, FreeEntry, SnapshotInfo, DiffAreaInfo>;

    static void Parse(BufferView buffer, CatalogEntry& entry, std::error_code& ec);

    template <typename T>
    const T* Get() const
    {
        return std::get_if<T>(&m_value);
    }

private:
    EntryHolder m_value;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
