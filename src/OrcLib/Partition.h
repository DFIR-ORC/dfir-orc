//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API Partition
{

    friend std::ostream& operator<<(std::ostream& o, const Orc::Partition& p);

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

    Partition(UINT8 partitionNumber = 0)
        : PartitionType(Type::Invalid)
        , SysFlags(0)
        , PartitionFlags(Flags::None)
        , PartitionNumber(partitionNumber)
        , SectorSize(0)
        , Start(0)
        , End(0)
        , Size(0) {};

    bool IsBootable() const
    {
        return (static_cast<std::int32_t>(PartitionFlags) & static_cast<std::int32_t>(Flags::Bootable)) > 0;
    }
    bool IsSystem() const
    {
        return (static_cast<std::int32_t>(PartitionFlags) & static_cast<std::int32_t>(Flags::System)) > 0;
    }
    bool IsReadOnly() const
    {
        return (static_cast<std::int32_t>(PartitionFlags) & static_cast<std::int32_t>(Flags::ReadOnly)) > 0;
    }
    bool IsHidden() const
    {
        return (static_cast<std::int32_t>(PartitionFlags) & static_cast<std::int32_t>(Flags::Hidden)) > 0;
    }
    bool IsNotAutoMountable() const
    {
        return (static_cast<std::int32_t>(PartitionFlags) & static_cast<std::int32_t>(Flags::NoAutoMount)) > 0;
    }

    bool IsFAT12() const { return PartitionType == Type::FAT12; }
    bool IsFAT16() const { return PartitionType == Type::FAT16; }
    bool IsFAT32() const { return PartitionType == Type::FAT32; }
    bool IsREFS() const { return PartitionType == Type::REFS; }
    bool IsNTFS() const { return PartitionType == Type::NTFS; }
    bool IsExtented() const { return PartitionType == Type::Extended; }
    bool IsGPT() const { return PartitionType == Type::GPT; }
    bool IsESP() const { return PartitionType == Type::ESP; }
    bool IsBitLocked() const { return PartitionType == Type::BitLocked; }
    bool IsMicrosoftReserved() const { return PartitionType == Type::MICROSOFT_RESERVED; }

    bool IsValid() const { return PartitionType != Type::Invalid; }

    Type PartitionType;
    UINT8 SysFlags;
    Flags PartitionFlags;
    UINT PartitionNumber;
    UINT SectorSize;
    UINT64 Start;
    UINT64 End;
    UINT64 Size;

    ~Partition(void) {};
};

std::wostream& operator<<(std::wostream& o, const Orc::Partition& p);

}  // namespace Orc

#pragma managed(pop)
