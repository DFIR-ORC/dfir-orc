//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#pragma once

#include "OrcLib.h"

#include "FatTableEntry.h"
#include "BinaryBuffer.h"

#include <algorithm>

#pragma managed(push, off)

namespace Orc {

class FatTable
{
public:
    enum FatTableType
    {
        FAT12,
        FAT16,
        FAT32
    };

    using ClusterChain = std::vector<FatTableEntry>;
    using FatTableChunks = std::vector<std::shared_ptr<CBinaryBuffer>>;

    FatTable(FatTableType type)
        : m_FatTableType(type)
    {
    }

    ~FatTable() {}

    bool IsFat12Table() const { return m_FatTableType == FAT12; }
    bool IsFat16Table() const { return m_FatTableType == FAT16; }
    bool IsFat32Table() const { return m_FatTableType == FAT32; }

    DWORD GetEntrySizeInBits() const
    {
        if (m_FatTableType == FAT12)
            return 12;
        if (m_FatTableType == FAT16)
            return 16;
        else
            return 32;
    }

    void AddChunk(const std::shared_ptr<CBinaryBuffer>& chunk) { mFatTableChunks.push_back(chunk); }

    const FatTableChunks& GetFatTableChunks() { return mFatTableChunks; }

    HRESULT FillClusterChain(ULONG firstClusterNumber, ClusterChain& clusterChain) const
    {
        clusterChain.clear();
        FatTableEntry entry(firstClusterNumber, GetEntrySizeInBits());

        do
        {
            clusterChain.push_back(entry);

            if (FAILED(GetEntry(entry.GetValue(), entry)))
                return E_FAIL;

        } while (entry.IsUsed());

        return S_OK;
    }

    HRESULT GetEntry(ULONG entryNumber, FatTableEntry& entry) const
    {
        ULONGLONG ulCurrentOffset = 0;
        ULONGLONG ulTargetOffset = (static_cast<ULONGLONG>(entryNumber) * GetEntrySizeInBits()) / 8;

        auto chunkIter = std::find_if(
            mFatTableChunks.begin(),
            mFatTableChunks.end(),
            [this, &ulCurrentOffset, ulTargetOffset, &entry](const std::shared_ptr<CBinaryBuffer>& chunk) -> bool {
                ULONGLONG ulChunkEndOffset = ulCurrentOffset + chunk->GetCount();

                if (ulTargetOffset >= ulCurrentOffset && ulTargetOffset < ulChunkEndOffset)
                {
                    return true;
                }

                ulCurrentOffset += chunk->GetCount();
                return false;
            });

        if (chunkIter != mFatTableChunks.end())
        {
            ULONGLONG index = ulTargetOffset - ulCurrentOffset;
            const std::shared_ptr<CBinaryBuffer>& buffer(*chunkIter);

            if (index >= buffer->GetCount())
                return E_FAIL;

            if (IsFat12Table())
            {
                if (entryNumber % 2 == 0)
                    entry = FatTableEntry(
                        *(reinterpret_cast<unsigned short*>(buffer->GetData() + index)) & 0x0FFF, GetEntrySizeInBits());
                else
                    entry = FatTableEntry(
                        (*(reinterpret_cast<unsigned short*>(buffer->GetData() + index)) & 0xFFF0) >> 4,
                        GetEntrySizeInBits());
            }
            else if (IsFat16Table())
            {
                entry = FatTableEntry(
                    *(reinterpret_cast<unsigned short*>(buffer->GetData() + index)), GetEntrySizeInBits());
            }
            else
                entry =
                    FatTableEntry(*(reinterpret_cast<unsigned long*>(buffer->GetData() + index)), GetEntrySizeInBits());

            return S_OK;
        }
        else
        {
            return E_FAIL;
        }
    }

private:
    FatTableChunks mFatTableChunks;
    FatTableType m_FatTableType;
};

}  // namespace Orc
#pragma managed(pop)
