//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl (ANSSI)
//
#include "stdafx.h"

#include "Partition.h"

#include "Text/Fmt/Partition.h"
#include "Text/Fmt/PartitionFlags.h"
#include "Text/Fmt/PartitionType.h"

namespace Orc {

bool Partition::IsBootable() const
{
    return HasFlag(PartitionFlags, Flags::Bootable);
}

bool Partition::IsSystem() const
{
    return HasFlag(PartitionFlags, Flags::System);
}

bool Partition::IsReadOnly() const
{
    return HasFlag(PartitionFlags, Flags::ReadOnly);
}

bool Partition::IsHidden() const
{
    return HasFlag(PartitionFlags, Flags::Hidden);
}

bool Partition::IsNotAutoMountable() const
{
    return HasFlag(PartitionFlags, Flags::NoAutoMount);
}

bool Partition::IsFAT12() const
{
    return PartitionType == Type::FAT12;
}

bool Partition::IsFAT16() const
{
    return PartitionType == Type::FAT16;
}

bool Partition::IsFAT32() const
{
    return PartitionType == Type::FAT32;
}

bool Partition::IsREFS() const
{
    return PartitionType == Type::REFS;
}

bool Partition::IsNTFS() const
{
    return PartitionType == Type::NTFS;
}

bool Partition::IsExtented() const
{
    return PartitionType == Type::Extended;
}

bool Partition::IsGPT() const
{
    return PartitionType == Type::GPT;
}

bool Partition::IsESP() const
{
    return PartitionType == Type::ESP;
}

bool Partition::IsBitLocked() const
{
    return PartitionType == Type::BitLocked;
}

bool Partition::IsMicrosoftReserved() const
{
    return PartitionType == Type::MICROSOFT_RESERVED;
}

bool Partition::IsValid() const
{
    return PartitionType != Type::Invalid;
}

std::wstring ToString(const Partition& partition)
{
    if (partition.IsValid())
    {
        return fmt::format(
            L"type: {}, number: {}, offsets: {:#x}-{:#x}, size: {}, flags: {}",
            partition.PartitionType,
            partition.PartitionNumber,
            partition.Start,
            partition.End,
            partition.Size,
            partition.PartitionFlags);
    }
    else
    {
        return fmt::format(L"Invalid, type: {}", partition.PartitionType);
    }
}

}  // namespace Orc
