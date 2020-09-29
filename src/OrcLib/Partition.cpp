//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl (ANSSI)
//
#include "stdafx.h"

#include "Partition.h"

#include <fmt/format.h>

#include <boost/algorithm/string/join.hpp>

namespace Orc {

std::wstring Partition::ToString(Type type)
{
    switch (type)
    {
        case Type::BitLocked:
            return L"Bitlocked";
        case Type::ESP:
            return L"ESP";
        case Type::Extended:
            return L"Extended";
        case Type::FAT12:
            return L"FAT12";
        case Type::FAT16:
            return L"FAT16";
        case Type::FAT32:
            return L"FAT32";
        case Type::GPT:
            return L"GPT";
        case Type::Invalid:
            return L"Invalid";
        case Type::MICROSOFT_RESERVED:
            return L"MICROSOFT_RESERVED";
        case Type::NTFS:
            return L"NTFS";
        case Type::Other:
            return L"Other";
        case Type::REFS:
            return L"REFS";
    }

    return L"Unsupported";
}

std::wstring Partition::ToString(Flags flags)
{
    std::vector<std::wstring> activeFlags;

    if (flags & Flags::Bootable)
    {
        activeFlags.push_back(L"BOOTABLE");
    }

    if (flags & Flags::Hidden)
    {
        activeFlags.push_back(L"HIDDEN");
    }

    if (flags & Flags::Invalid)
    {
        activeFlags.push_back(L"INVALID");
    }

    if (flags & Flags::NoAutoMount)
    {
        activeFlags.push_back(L"NO_AUTO_MOUNT");
    }

    if (flags & Flags::None)
    {
        activeFlags.push_back(L"NONE");
    }

    if (flags & Flags::ReadOnly)
    {
        activeFlags.push_back(L"READONLY");
    }

    if (flags & Flags::System)
    {
        activeFlags.push_back(L"SYSTEM");
    }

    return boost::join(activeFlags, L" ");
}

bool Partition::IsBootable() const
{
    return PartitionFlags & Flags::Bootable;
}

bool Partition::IsSystem() const
{
    return PartitionFlags & Flags::System;
}

bool Partition::IsReadOnly() const
{
    return PartitionFlags & Flags::ReadOnly;
}

bool Partition::IsHidden() const
{
    return PartitionFlags & Flags::Hidden;
}

bool Partition::IsNotAutoMountable() const
{
    return PartitionFlags & Flags::NoAutoMount;
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

std::wstring Partition::ToString(const Partition& partition)
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
