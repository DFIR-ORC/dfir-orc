//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "OfflineMFTReader.h"

#include <spdlog/spdlog.h>

using namespace Orc;

OfflineMFTReader::OfflineMFTReader(const WCHAR* szMFTFileName)
    : VolumeReader(szMFTFileName)
{
    m_hMFT = INVALID_HANDLE_VALUE;
    wcsncpy_s(m_szMFTFileName, szMFTFileName, MAX_PATH);
    m_cOriginalName = L'C';
    m_BytesPerCluster = 0x1000;
    m_BytesPerSector = 0x200;
    m_BytesPerFRS = 0x400;
    m_llVolumeSerialNumber = 0L;
    m_dwMaxComponentLength = 255;
    m_fsType = FSVBR::FSType::NTFS;
}

HRESULT OfflineMFTReader::SetCharacteristics(
    WCHAR VolumeOriginalName,
    ULONG ulBytesPerSector,
    ULONG ulBytesPerCluster,
    ULONG ulBytesPerFRS)
{
    m_cOriginalName = VolumeOriginalName;
    m_BytesPerCluster = ulBytesPerCluster;
    m_BytesPerSector = ulBytesPerSector;
    m_BytesPerFRS = ulBytesPerFRS;
    return S_OK;
}

HRESULT OfflineMFTReader::SetCharacteristics(const CBinaryBuffer& buffer)
{
    ParseBootSector(buffer);
    return S_OK;
}

HRESULT OfflineMFTReader::LoadDiskProperties()
{
    HRESULT hr = E_FAIL;

    if (m_hMFT != INVALID_HANDLE_VALUE)
    {
        return S_OK;
    }

    if ((m_hMFT = CreateFile(
             m_szMFTFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL))
        == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        spdlog::error(L"Could not open offline MFT file '{}' (code: {:#x})", m_szMFTFileName, hr);
        return hr;
    }

    return S_OK;
}

HRESULT OfflineMFTReader::Seek(ULONGLONG offset)
{
    DBG_UNREFERENCED_PARAMETER(offset);
    return HRESULT_FROM_WIN32(ERROR_INVALID_OPERATION);
}

HRESULT OfflineMFTReader::Read(ULONGLONG offset, CBinaryBuffer& data, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead)
{
    DBG_UNREFERENCED_PARAMETER(offset);
    DBG_UNREFERENCED_PARAMETER(data);
    DBG_UNREFERENCED_PARAMETER(ullBytesToRead);
    DBG_UNREFERENCED_PARAMETER(ullBytesRead);

    return HRESULT_FROM_WIN32(ERROR_INVALID_OPERATION);
}

HRESULT OfflineMFTReader::Read(CBinaryBuffer& data, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead)
{
    DBG_UNREFERENCED_PARAMETER(data);
    DBG_UNREFERENCED_PARAMETER(ullBytesToRead);
    DBG_UNREFERENCED_PARAMETER(ullBytesRead);
    return HRESULT_FROM_WIN32(ERROR_INVALID_OPERATION);
}

std::shared_ptr<VolumeReader> OfflineMFTReader::ReOpen(DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwFlags)
{
    return DuplicateReader();
}

std::shared_ptr<VolumeReader> OfflineMFTReader::DuplicateReader()
{
    auto retval = std::make_shared<OfflineMFTReader>(m_szLocation);

    retval->m_cOriginalName = m_cOriginalName;
    wcscpy_s(retval->m_szMFTFileName, m_szMFTFileName);

    DuplicateHandle(
        GetCurrentProcess(), m_hMFT, GetCurrentProcess(), &retval->m_hMFT, 0L, FALSE, DUPLICATE_SAME_ACCESS);

    return retval;
}

OfflineMFTReader::~OfflineMFTReader(void)
{
    if (m_hMFT != INVALID_HANDLE_VALUE)
        CloseHandle(m_hMFT);
}
