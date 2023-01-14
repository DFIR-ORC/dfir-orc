//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "CompleteVolumeReader.h"
#include "ByteStream.h"
#include "Kernel32Extension.h"

#include "Log/Log.h"

using namespace Orc;

CompleteVolumeReader::CompleteVolumeReader(const WCHAR* szLocation)
    : VolumeReader(szLocation)
{
    m_bCanReadData = true;
}

HRESULT CompleteVolumeReader::Seek(ULONGLONG offset)
{

    if (m_BytesPerSector && offset % m_BytesPerSector)
    {
        // we round to the lower sector
        m_LocalPositionOffset = offset % m_BytesPerSector;
        offset = (offset / m_BytesPerSector) * m_BytesPerSector;
    }
    else
    {
        m_LocalPositionOffset = 0L;
    }

    LARGE_INTEGER liPosition;
    liPosition.QuadPart = (LONGLONG)(offset);

    return m_Extents[0].Seek(liPosition, NULL, FILE_BEGIN);
}

HRESULT
CompleteVolumeReader::ReadUnaligned(
    ULONGLONG offset,
    CBinaryBuffer& data,
    ULONGLONG bytesToRead,
    ULONGLONG& ullBytesRead)
{
    // Unaligned read
    if (m_BytesPerSector != 0)
    {
        Log::Error("Cannot call this function with aligned volume reader");
        return E_FAIL;
    }

    HRESULT hr = E_FAIL;
    if (data.OwnsBuffer() && !data.SetCount(static_cast<size_t>(bytesToRead)))
    {
        return E_OUTOFMEMORY;
    }

    hr = Seek(offset);
    if (FAILED(hr))
    {
        return hr;
    }

    assert(m_LocalPositionOffset == 0 && "CompleteVolumeReader: unexpected internal offset");

    DWORD dwBytesRead = 0;
    hr = m_Extents[0].Read(data.GetData(), data.GetCount(), &dwBytesRead);
    ullBytesRead = dwBytesRead;
    return hr;
}

HRESULT
CompleteVolumeReader::Read(ULONGLONG offset, CBinaryBuffer& data, ULONGLONG bytesToRead, ULONGLONG& ullBytesRead)
{
    HRESULT hr = E_FAIL;

    concurrency::critical_section::scoped_lock sl(m_cs);

    // Unaligned read
    if (m_BytesPerSector == 0)
    {
        return ReadUnaligned(offset, data, bytesToRead, ullBytesRead);
    }

    hr = Seek(offset);
    if (FAILED(hr))
    {
        return hr;
    }

    const auto initialOffset = m_Extents[0].GetSeekOffset() + m_LocalPositionOffset;

    ULONGLONG alignedBytesToRead;
    if (bytesToRead % m_BytesPerSector || m_LocalPositionOffset > 0)
    {
        alignedBytesToRead = (((bytesToRead + m_LocalPositionOffset) / m_BytesPerSector) + 1) * m_BytesPerSector;
    }
    else
    {
        alignedBytesToRead = bytesToRead;
    }

    CBinaryBuffer localReadBuffer(true);
    if (!localReadBuffer.CheckCount(static_cast<size_t>(alignedBytesToRead)))
    {
        return E_OUTOFMEMORY;
    }

    DWORD dwBytesRead = 0;
    hr = m_Extents[0].Read(localReadBuffer.GetData(), alignedBytesToRead, &dwBytesRead);
    if (FAILED(hr))
    {
        return hr;
    }

    if (dwBytesRead <= m_LocalPositionOffset)
    {
        Log::Warn("Unexpected! Read was done completely for alignement and must be called another time");
        m_LocalPositionOffset = m_LocalPositionOffset - dwBytesRead;

        hr = Read(initialOffset + dwBytesRead, data, bytesToRead, ullBytesRead);
        // TODO: think about it
        return hr;
    }

    ullBytesRead = std::min(static_cast<ULONGLONG>(dwBytesRead - m_LocalPositionOffset), bytesToRead);

    // TODO: this trigger a reallocation even if smaller
    data.SetCount(ullBytesRead);

    CopyMemory(data.GetData(), localReadBuffer.GetData() + m_LocalPositionOffset, static_cast<size_t>(ullBytesRead));

    hr = Seek(initialOffset + ullBytesRead);
    if (FAILED(hr))
    {
        return hr;
    }

    localReadBuffer.RemoveAll();
    m_LocalPositionOffset = 0;
    return S_OK;
}

std::shared_ptr<VolumeReader> CompleteVolumeReader::ReOpen(DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwFlags)
{
    auto retval = DuplicateReader();

    auto complete_reader = std::dynamic_pointer_cast<CompleteVolumeReader>(retval);

    for (const auto& extent : m_Extents)
    {
        complete_reader->m_Extents.push_back(extent.ReOpen(dwDesiredAccess, dwShareMode, dwFlags));
    }

    return retval;
}

// Read from disk.
HRESULT CompleteVolumeReader::Read(CBinaryBuffer& data, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead)
{
    Log::Critical("Not implemented: CompleteVolumeReader::Read");
    return E_NOTIMPL;
}

HRESULT CompleteVolumeReader::ParseBootSector()
{
    if (FSVBR::FSType::UNKNOWN != m_fsType)
        return S_OK;

    HRESULT hr = E_FAIL;

    if (!m_Extents.empty())
    {
        LARGE_INTEGER liDistance = {0, 0};
        LARGE_INTEGER liNewPos = {0, 0};

        if (FAILED(hr = m_Extents[0].Seek(liDistance, &liNewPos, FILE_BEGIN)))
            return hr;

        CBinaryBuffer buffer;
        if (!buffer.SetCount(sizeof(PackedGenBootSector)))
            return E_OUTOFMEMORY;

        DWORD dwBytesRead = 0;

        if (FAILED(hr = m_Extents[0].Read(buffer.GetData(), sizeof(PackedGenBootSector), &dwBytesRead)))
        {
            Log::Error("Failed to read the first 512 bytes [{}]", SystemError(hr));

            if (!buffer.SetCount(4096))
                return E_OUTOFMEMORY;

            if (FAILED(hr = m_Extents[0].Read(buffer.GetData(), 4096, &dwBytesRead)))
            {
                Log::Error("Failed to read the first 4096 bytes [{}]", SystemError(hr));
                return hr;
            }
        }

        return VolumeReader::ParseBootSector(buffer);
    }

    return hr;
}

CompleteVolumeReader::~CompleteVolumeReader() {}
