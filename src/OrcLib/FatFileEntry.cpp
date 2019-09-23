//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#include "stdafx.h"

#include "FatFileEntry.h"
#include "FSUtils.h"

#include <sstream>

using namespace Orc;

FatFileEntry::FatFileEntry()
    : m_Name(L"?")
    , m_LongName(L"")
    , m_ulSize(0)
    , m_Checksum(0x0)
    , m_Attributes(0)
    , m_bIsFolder(false)
    , m_ParentFolder(nullptr)
    , m_bIsDeleted(false)
{
    ZeroMemory(&m_CreationTime, sizeof(m_CreationTime));
    ZeroMemory(&m_ModificationTime, sizeof(m_ModificationTime));
    ZeroMemory(&m_AccessTime, sizeof(m_AccessTime));
}

const std::wstring& FatFileEntry::GetLongName() const
{
    if (m_LongName.empty())
        return GetShortName();
    else
        return m_LongName;
}

const std::wstring FatFileEntry::GetFullName() const
{
    std::wstring fullName = GetLongName();

    const FatFileEntry* parent = m_ParentFolder;
    while (parent != nullptr)
    {
        fullName = parent->GetLongName() + L"\\" + fullName;
        parent = parent->m_ParentFolder;
    }

    fullName = L"\\" + fullName;

    return fullName;
}

/*
 * we could be more smart here and merge contiguous segments
 */
void FatFileEntry::FillSegmentDetailsMap(ULONGLONG rootDirectoryOffset, ULONG ulClusterSize)
{
    if (m_ClusterChain.empty())
        return;

    m_SegmentDetailsMap.clear();
    ULONG ulTotalSize = 0;

    std::for_each(
        std::begin(m_ClusterChain),
        std::end(m_ClusterChain),
        [this, &ulTotalSize, rootDirectoryOffset, ulClusterSize](const FatTableEntry& entry) {
            ULONG clusterNumber = 0;

            if (m_ulSize > ulTotalSize && (entry.IsUsed() && (clusterNumber = entry.GetValue()) >= 2))
            {
                SegmentDetails segmentDetails;
                ULONGLONG startOffset =
                    rootDirectoryOffset + ((ULONGLONG)(clusterNumber - 2) * (ULONGLONG)ulClusterSize);
                ULONGLONG endOffset = 0;

                segmentDetails.mStartOffset = startOffset;

                if (m_ulSize > ulTotalSize + ulClusterSize)
                {
                    segmentDetails.mEndOffset = startOffset + ulClusterSize;
                    segmentDetails.mSize = ulClusterSize;
                }
                else
                {
                    segmentDetails.mSize = m_ulSize - ulTotalSize;
                    segmentDetails.mEndOffset = startOffset + (segmentDetails.mSize);
                }

                m_SegmentDetailsMap.insert(std::pair<ULONGLONG, SegmentDetails>(ulTotalSize, segmentDetails));
                ulTotalSize += segmentDetails.mSize;
            }
        });
}

std::wostream& Orc::operator<<(std::wostream& os, const FatFileEntry& fatFatFileEntry)
{
    os << fatFatFileEntry.m_Name;

    if (!fatFatFileEntry.m_LongName.empty())
        os << L" - Long name : " << fatFatFileEntry.m_LongName;

    os << L" - Full name : " << fatFatFileEntry.GetFullName();
    os << L" - size : " << fatFatFileEntry.m_ulSize;

    if (fatFatFileEntry.IsFolder())
        os << L" - FOLDER";

    if (fatFatFileEntry.IsDeleted())
    {
        os << L" - DELETED";
    }

    // times
    FatFileEntry::StreamDate(os, std::wstring(L" - creation time : "), fatFatFileEntry.m_CreationTime);
    FatFileEntry::StreamDate(os, std::wstring(L" - modification time : "), fatFatFileEntry.m_ModificationTime);
    FatFileEntry::StreamDate(os, std::wstring(L" - last access time : "), fatFatFileEntry.m_AccessTime);

    // attributes
    os << L" - attributes : ";

    std::wstringstream attributes;

    if (fatFatFileEntry.IsSystem())
    {
        attributes << L" SYSTEM";
    }
    if (fatFatFileEntry.IsHidden())
    {
        attributes << L" HIDDEN";
    }
    if (fatFatFileEntry.IsReadOnly())
    {
        attributes << L" RO";
    }

    if (attributes.str().empty())
    {
        os << L" NONE";
    }
    else
    {
        os << attributes.str();
    }

    return os;
}

HRESULT FatFileEntry::StreamDate(std::wostream& os, const std::wstring& dateTypeStr, const FILETIME& time)
{
    SYSTEMTIME systemTime;
    FileTimeToSystemTime(&time, &systemTime);

    const DWORD dwMaxChar = 64;
    WCHAR szBuffer[dwMaxChar];

    if (!FAILED(StringCchPrintfW(
            szBuffer,
            dwMaxChar,
            L"%u-%02u-%02u %02u:%02u:%02u.%03u",
            systemTime.wYear,
            systemTime.wMonth,
            systemTime.wDay,
            systemTime.wHour,
            systemTime.wMinute,
            systemTime.wSecond,
            systemTime.wMilliseconds)))
    {
        os << dateTypeStr << szBuffer;
        return S_OK;
    }
    else
    {
        return E_FAIL;
    }
}
