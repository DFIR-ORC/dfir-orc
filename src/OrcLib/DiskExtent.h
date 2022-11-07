//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "IDiskExtent.h"

#pragma managed(push, off)

namespace Orc {

class CDiskExtent : public IDiskExtent
{
    friend class CompleteVolumeReader;
    friend class PartitionTable;
    friend class VHDVolumeReader;
    friend class FixedVHDVolumeReader;
    friend class MountedVolumeReader;
    friend class SnapshotVolumeReader;
    friend class InterfaceReader;
    friend class ImageReader;
    friend class LocationSet;
    friend class PhysicalDiskReader;
    friend class SystemStorageReader;

public:
    // CDiskExtent
    CDiskExtent() {}

    CDiskExtent(const std::wstring& name, ULONGLONG ullStart, ULONGLONG ullLength, ULONG ulSectorSize);
    CDiskExtent(const std::wstring& name);
    CDiskExtent(CDiskExtent&& Other) noexcept;
    CDiskExtent(const CDiskExtent& Other) noexcept;

    CDiskExtent& operator=(const CDiskExtent&);

private:
    std::wstring m_Name;  // This can be either a physical drive name, a volume name, an image file name...
    ULONGLONG m_Start = 0LLU;
    ULONGLONG m_Length = 0LLU;

    LARGE_INTEGER m_liCurrentPos = {0};

    ULONG m_LogicalSectorSize = 0LU;
    ULONG m_PhysicalSectorSize = 0LU;
    HANDLE m_hFile = INVALID_HANDLE_VALUE;

public:
    // from IDiskExtend
    virtual HRESULT Open(DWORD dwShareMode, DWORD dwCreationDisposition, DWORD dwFlags);
    virtual HRESULT Seek(LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER pliNewFilePointer, DWORD dwFrom);
    virtual HRESULT Read(__in_bcount(dwCount) PVOID lpBuf, DWORD dwCount, PDWORD pdwBytesRead);
    virtual void Close();

    virtual const std::wstring& GetName() const { return m_Name; }
    virtual ULONGLONG GetStartOffset() const { return m_Start; }
    virtual ULONGLONG GetSeekOffset() const { return m_liCurrentPos.QuadPart - m_Start; }
    virtual ULONGLONG GetLength() const { return m_Length; }
    virtual ULONG GetLogicalSectorSize() const { return m_LogicalSectorSize; }
    virtual HANDLE GetHandle() const { return m_hFile; };

    virtual CDiskExtent ReOpen(DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwFlags) const;

    virtual ~CDiskExtent();
};

}  // namespace Orc

#pragma managed(pop)
