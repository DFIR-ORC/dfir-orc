//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "PartitionFlags.h"
#include "PartitionType.h"
#include "Output/Text/Fmt/Partition.h"
#include "Output/Text/Fmt/PartitionFlags.h"
#include "Output/Text/Fmt/PartitionType.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API Partition
{
public:
    using Type = PartitionType;
    using Flags = PartitionFlags;

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

    ~Partition(void) {};
};

std::wstring ToString(const Partition& partition);

}  // namespace Orc

#pragma managed(pop)
