//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#pragma once

#include "OrcLib.h"

#include "BinaryBuffer.h"

#pragma managed(push, off)

namespace Orc {

static unsigned long FAT12_BAD_CLUSTER = 0xFF7;
static unsigned long FAT12_MAX_ENTRY_VALUE = 0xFFF;

static unsigned long FAT16_BAD_CLUSTER = 0xFFF7;
static unsigned long FAT16_MAX_ENTRY_VALUE = 0xFFFF;

static unsigned long FAT32_BAD_CLUSTER = 0x0FFFFFF7;
static unsigned long FAT32_MAX_ENTRY_VALUE = 0x0FFFFFFF;

class FatTableEntry
{
public:
    FatTableEntry()
        : m_EntryValue(0)
        , m_EntrySizeInBits(0)
    {
    }

    FatTableEntry(ULONG ulEntryValue, DWORD entrySizeInBits)
        : m_EntryValue(ulEntryValue)
        , m_EntrySizeInBits(entrySizeInBits)
    {
    }

    boolean IsFat12Entry() const { return m_EntrySizeInBits == 12; }
    boolean IsFat16Entry() const { return m_EntrySizeInBits == 16; }
    boolean IsFat32Entry() const { return m_EntrySizeInBits == 32; }
    ULONG GetValue() const { return m_EntryValue; }

    bool IsFree() const { return m_EntryValue == 0x0; }

    bool IsUsed() const { return m_EntryValue > 0 && m_EntryValue < GetBadCluster(); }

    bool IsBadEntry() const { return m_EntryValue == GetBadCluster(); }

    bool IsEOFEntry() const { return m_EntryValue > GetBadCluster() && m_EntryValue <= GetMaxEntryValue(); }

private:
    DWORD GetBadCluster() const
    {
        if (IsFat12Entry())
            return FAT12_BAD_CLUSTER;
        else if (IsFat16Entry())
            return FAT16_BAD_CLUSTER;
        else
            return FAT32_BAD_CLUSTER;
    }

    DWORD GetMaxEntryValue() const
    {
        if (IsFat12Entry())
            return FAT12_MAX_ENTRY_VALUE;
        else if (IsFat16Entry())
            return FAT16_MAX_ENTRY_VALUE;
        else
            return FAT32_MAX_ENTRY_VALUE;
    }

    ULONG m_EntryValue;
    DWORD m_EntrySizeInBits;
};
}  // namespace Orc

#pragma managed(pop)
