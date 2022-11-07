//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "VolumeReaderVisitor.h"
#include "DiskExtent.h"
#include "FSVBR.h"

#pragma managed(push, off)

namespace Orc {

class VolumeReader
{
public:
    using CDiskExtentVector = std::vector<CDiskExtent>;

public:
    VolumeReader(const WCHAR* szLocation);

    virtual void Accept(VolumeReaderVisitor& visitor) const { return visitor.Visit(*this); }

    virtual const WCHAR* ShortVolumeName() PURE;

    const WCHAR* GetLocation() const { return m_szLocation; }
    FSVBR::FSType GetFSType() const { return m_fsType; }

    ULONGLONG VolumeSerialNumber() const { return m_llVolumeSerialNumber; }
    DWORD MaxComponentLength() const
    {
        DWORD length = m_dwMaxComponentLength > 0 ? m_dwMaxComponentLength : 255;
        if (length < 32768)
        {
            length = 32768;
        }

        return length;
    }

    virtual HRESULT LoadDiskProperties() PURE;
    virtual HANDLE GetDevice() PURE;

    bool IsReady() { return m_bReadyForEnumeration; }

    virtual ULONG GetBytesPerFRS() const { return m_BytesPerFRS; };
    virtual ULONG GetBytesPerCluster() const { return m_BytesPerCluster; }
    virtual ULONG GetBytesPerSector() const { return m_BytesPerSector; }

    virtual HRESULT Seek(ULONGLONG offset) PURE;
    virtual HRESULT Read(ULONGLONG offset, CBinaryBuffer& data, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead) = 0;
    virtual HRESULT Read(CBinaryBuffer& data, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead) = 0;

    HRESULT Read(const LPBYTE data, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead);

    const CBinaryBuffer& GetBootSector() const { return m_BoostSector; }

    virtual std::shared_ptr<VolumeReader> ReOpen(DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwFlags) PURE;

    virtual ~VolumeReader() {}

protected:
    bool m_bCanReadData = true;
    bool m_bReadyForEnumeration = false;
    ULONG m_BytesPerFRS = 0L;
    ULONG m_BytesPerSector = 0L;
    ULONG m_BytesPerCluster = 0L;
    ULONG m_LocalPositionOffset = 0L;
    LONGLONG m_NumberOfSectors = 0LL;
    ULONGLONG m_llVolumeSerialNumber = 0LL;
    DWORD m_dwMaxComponentLength = 0L;
    FSVBR::FSType m_fsType = FSVBR::FSType::UNKNOWN;

    // Represents the "Volume" being read.
    // this could be either a mounted volume, a dd image, an offline MFT, a snapshot, etc...
    WCHAR m_szLocation[ORC_MAX_PATH];

    HRESULT ParseBootSector(const CBinaryBuffer& buffer);
    CBinaryBuffer m_BoostSector;

    virtual std::shared_ptr<VolumeReader> DuplicateReader() PURE;
};

}  // namespace Orc

#pragma managed(pop)
