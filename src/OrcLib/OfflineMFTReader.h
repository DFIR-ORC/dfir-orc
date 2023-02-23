//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "VolumeReader.h"

#pragma managed(push, off)

namespace Orc {

class OfflineMFTReader : public VolumeReader
{
private:
    WCHAR m_szMFTFileName[ORC_MAX_PATH];
    WCHAR m_cOriginalName;
    HANDLE m_hMFT;

protected:
    virtual std::shared_ptr<VolumeReader> DuplicateReader();

public:
    OfflineMFTReader(const WCHAR* szMFTFileName);

    void Accept(VolumeReaderVisitor& visitor) const override { return visitor.Visit(*this); }

    HRESULT
    SetCharacteristics(WCHAR VolumeOriginalName, ULONG ulBytesPerSector, ULONG ulBytesPerCluster, ULONG ulBytesPerFRS);
    HRESULT SetCharacteristics(const CBinaryBuffer& buffer);

    const WCHAR* ShortVolumeName() { return L"\\"; }
    HANDLE GetHandle() { return m_hMFT; }

    HRESULT LoadDiskProperties(void);
    HANDLE GetDevice() { return INVALID_HANDLE_VALUE; }

    HRESULT Seek(ULONGLONG offset);
    HRESULT Read(ULONGLONG offset, CBinaryBuffer& data, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead);
    HRESULT Read(CBinaryBuffer& data, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead);

    virtual std::shared_ptr<VolumeReader> ReOpen(DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwFlags);

    ~OfflineMFTReader(void);
};

}  // namespace Orc

#pragma managed(pop)
