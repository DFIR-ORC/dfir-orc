//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#pragma once

#include "OrcLib.h"

#include "FatTable.h"
#include "SegmentDetails.h"

#include <string>
#include <map>

#pragma managed(push, off)

namespace Orc {

class FatFileEntry
{
public:
    friend std::wostream& operator<<(std::wostream& os, const FatFileEntry& fatFileEntry);
    FatFileEntry();

    const std::wstring& GetShortName() const { return m_Name; }
    const std::wstring& GetLongName() const;
    const std::wstring GetFullName() const;
    DWORD GetSize() const { return m_ulSize; }

    bool IsSystem() const { return (m_Attributes & FILE_ATTRIBUTE_SYSTEM) > 0; }
    bool IsReadOnly() const { return (m_Attributes & FILE_ATTRIBUTE_READONLY) > 0; }
    bool IsHidden() const { return (m_Attributes & FILE_ATTRIBUTE_HIDDEN) > 0; }

    bool IsFolder() const { return m_bIsFolder; }
    bool IsDeleted() const { return m_bIsDeleted; }

    const FatTable::ClusterChain& GetClusterChain() const { return m_ClusterChain; }
    void FillSegmentDetailsMap(ULONGLONG rootDirectoryOffset, ULONG ulClusterSize);

    static HRESULT StreamDate(std::wostream& os, const std::wstring& dateTypeStr, const FILETIME& time);

    std::wstring m_Name;
    std::wstring m_LongName;
    DWORD m_ulSize;
    UCHAR m_Checksum;
    DWORD m_Attributes;
    FatTable::ClusterChain m_ClusterChain;
    SegmentDetailsMap m_SegmentDetailsMap;

    FILETIME m_CreationTime;
    FILETIME m_ModificationTime;
    FILETIME m_AccessTime;

    bool m_bIsFolder;
    const FatFileEntry* m_ParentFolder;
    bool m_bIsDeleted;
};

}  // namespace Orc

#pragma managed(pop)
