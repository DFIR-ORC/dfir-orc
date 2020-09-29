//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "Utils/EnumFlags.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API Partition
{
public:
    enum class Type
    {
        Invalid,
        FAT12,
        FAT16,
        FAT32,
        NTFS,
        REFS,
        Extended,
        GPT,
        ESP,
        MICROSOFT_RESERVED,
        BitLocked,
        Other
    };

    static std::wstring ToString(Type type);

    enum class Flags : std::int32_t
    {
        None = 0x0,
        Invalid = 0x1,
        Bootable = 0x10,
        System = 0x100,
        ReadOnly = 0x1000,
        Hidden = 0x10000,
        NoAutoMount = 0x100000
    };

    static std::wstring ToString(Flags flags);

    Partition(UINT8 partitionNumber = 0)
        : PartitionType(Type::Invalid)
        , SysFlags(0)
        , PartitionFlags(Flags::None)
        , PartitionNumber(partitionNumber)
        , SectorSize(0)
        , Start(0)
        , End(0)
        , Size(0) {};

    bool IsBootable() const;
    bool IsSystem() const;
    bool IsReadOnly() const;
    bool IsHidden() const;
    bool IsNotAutoMountable() const;

    bool IsFAT12() const;
    bool IsFAT16() const;
    bool IsFAT32() const;
    bool IsREFS() const;
    bool IsNTFS() const;
    bool IsExtented() const;
    bool IsGPT() const;
    bool IsESP() const;
    bool IsBitLocked() const;
    bool IsMicrosoftReserved() const;

    bool IsValid() const;

    Type PartitionType;
    UINT8 SysFlags;
    Flags PartitionFlags;
    UINT PartitionNumber;
    UINT SectorSize;
    UINT64 Start;
    UINT64 End;
    UINT64 Size;

    static std::wstring ToString(const Partition& partition);

    ~Partition(void) {};
};

ENABLE_BITMASK_OPERATORS(Partition::Flags);

}  // namespace Orc

#pragma managed(pop)
