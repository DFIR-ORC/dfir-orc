//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "DiskStructures.h"
#include "Partition.h"

#include <map>

#pragma managed(push, off)

namespace Orc {

class IDiskExtent;

class PartitionTable
{
public:
    PartitionTable();
    ~PartitionTable(void);

    typedef std::vector<Partition> PartitionVector;
    const PartitionVector& Table() const { return m_Table; }

    HRESULT LoadPartitionTable(const WCHAR* szDeviceOrImage);
    HRESULT LoadPartitionTable(IDiskExtent& diskExtend);

    // static methods
    static HRESULT GuessPartitionType(const CBinaryBuffer& buffer, Partition& p);
    static void ParseDiskPartition(DiskPartition* diskPartition, UINT sectorSize, Partition& p, ULONG64 partitionOffset);
    static void
    ParseDiskPartition(IDiskExtent& diskExtend, GPTPartitionEntry* gptPartitionEntry, UINT sectorSize, Partition& p);
    static UINT GetSectorSize(IDiskExtent& extent);
    bool isGPT() { return m_isGPT; };

private:
    // partition table methods
    HRESULT ParseMBRPartitionTable(IDiskExtent& diskExtend, UINT sectorSize, PMasterBootRecord pMBR);
    HRESULT ParseGPTPartitionTable(IDiskExtent& diskExtend, UINT sectorSize, PGPTHeader pGPTHeader);
    HRESULT ParseExtendedPartition(
        IDiskExtent& diskExtend,
        const Partition& p,
        ExtendedBootRecord* pExtendedRecord,
        ULONG64 u64ExtendedPartitionStart);

    void AddPartition(const Partition& p);
    UINT8 GetPartitionNumber();

    // internal methods
    HRESULT OpenDiskExtend(IDiskExtent& diskExtend);
    HRESULT SeekDiskExtend(IDiskExtent& diskExtend, ULONG64 offset);
    HRESULT ReadDiskExtend(IDiskExtent& diskExtend, CBinaryBuffer& buffer);

    DWORD Crc32(const CBinaryBuffer& buffer);

private:
    std::vector<Partition> m_Table;
    IDiskExtent* m_pDiskExtent;
    bool m_isGPT = false;
};

}  // namespace Orc
#pragma managed(pop)
