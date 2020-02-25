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

using namespace Orc;

CompleteVolumeReader::CompleteVolumeReader(logger pLog, const WCHAR* szLocation)
    : VolumeReader(std::move(pLog), szLocation)
{
    m_bCanReadData = true;
}

HRESULT CompleteVolumeReader::Seek(ULONGLONG offset)
{
    if (offset > m_Extents[0].GetLength())
        return E_INVALIDARG;

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
CompleteVolumeReader::Read(ULONGLONG offset, CBinaryBuffer& data, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead)
{
    HRESULT hr = E_FAIL;

    concurrency::critical_section::scoped_lock sl(m_cs);

    if (FAILED(hr = Seek(offset)))
        return hr;

    if (m_LocalPositionOffset > 0)
    {
        CBinaryBuffer localReadBuffer(true);
        ULONGLONG ullBytesToReadWithOffset =
            (((ullBytesToRead + m_LocalPositionOffset) / m_BytesPerSector) + 1) * m_BytesPerSector;

        if (data.GetCount() < ullBytesToReadWithOffset)
        {
            if (data.OwnsBuffer())
            {
                if (!localReadBuffer.CheckCount(static_cast<size_t>(ullBytesToReadWithOffset)))
                    return E_OUTOFMEMORY;
                ULONGLONG ullBytesReadWithOffset = 0LL;

                if (FAILED(hr = Read(localReadBuffer, ullBytesToReadWithOffset, ullBytesReadWithOffset)))
                    return hr;

                if (ullBytesReadWithOffset > m_LocalPositionOffset)
                    ullBytesRead = ullBytesReadWithOffset - m_LocalPositionOffset;
                else
                    ullBytesRead = 0LL;

                if (ullBytesRead > ullBytesToRead)
                    ullBytesRead = ullBytesToRead;
                if (ullBytesRead > 0)
                {
                    if (!data.SetCount(static_cast<size_t>(ullBytesRead)))
                    {
                        ullBytesRead = 0LL;
                        return E_OUTOFMEMORY;
                    }
                    CopyMemory(
                        data.GetData(),
                        localReadBuffer.GetData() + m_LocalPositionOffset,
                        static_cast<size_t>(ullBytesRead));
                    localReadBuffer.RemoveAll();
                }
                m_LocalPositionOffset = 0;
            }
            else
            {
                if (!localReadBuffer.CheckCount(static_cast<size_t>(ullBytesToReadWithOffset)))
                    return E_OUTOFMEMORY;

                ULONGLONG ullBytesReadWithOffset = 0LL;
                if (FAILED(hr = Read(localReadBuffer, ullBytesToReadWithOffset, ullBytesReadWithOffset)))
                    return hr;

                if (ullBytesReadWithOffset > m_LocalPositionOffset)
                    ullBytesRead = ullBytesReadWithOffset - m_LocalPositionOffset;
                else
                    ullBytesRead = 0LL;

                if (ullBytesRead > ullBytesToRead)
                    ullBytesRead = ullBytesToRead;
                if (ullBytesRead > 0)
                {
                    CopyMemory(
                        data.GetData(),
                        localReadBuffer.GetData() + m_LocalPositionOffset,
                        static_cast<size_t>(ullBytesRead));
                    localReadBuffer.RemoveAll();
                }
                m_LocalPositionOffset = 0L;
            }
        }
        else
        {
            ULONGLONG ullBytesReadWithOffset = 0LL;
            if (FAILED(hr = Read(data, ullBytesToReadWithOffset, ullBytesReadWithOffset)))
                return hr;

            if (ullBytesReadWithOffset > m_LocalPositionOffset)
                ullBytesRead = ullBytesReadWithOffset - m_LocalPositionOffset;
            else
                ullBytesRead = 0LL;

            if (ullBytesRead > ullBytesToRead)
                ullBytesRead = ullBytesToRead;
            if (ullBytesRead > 0)
            {
                MoveMemory(data.GetData(), data.GetData() + m_LocalPositionOffset, static_cast<size_t>(ullBytesRead));
                localReadBuffer.RemoveAll();
            }
            m_LocalPositionOffset = 0L;
        }
    }
    else
    {
        if (FAILED(hr = Read(data, ullBytesToRead, ullBytesRead)))
            return hr;
    }
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
    HRESULT hr = E_FAIL;

    ullBytesRead = 0LL;
    CDiskExtent& Extent = m_Extents[0];

    //
    // how big do we want the read to be?
    //
    DWORD chunk = (DWORD)std::min((ULONGLONG)DEFAULT_READ_SIZE, ullBytesToRead);

    if (m_BytesPerSector && chunk % m_BytesPerSector)
    {
        // we round to the upper sector
        chunk = ((chunk / m_BytesPerSector) + 1) * m_BytesPerSector;
    }

    if (data.OwnsBuffer())
    {
        // reserve space in the
        if (!data.SetCount(chunk))
            return E_OUTOFMEMORY;

        CBinaryBuffer localReadBuffer(true);

        if (m_BytesPerSector && ((INT_PTR)data.GetData() % m_BytesPerSector))
        {
            if (!localReadBuffer.CheckCount(chunk))
                return E_OUTOFMEMORY;

            //
            // now read
            //
            DWORD dwBytesRead = 0;
            if (FAILED(hr = Extent.Read(localReadBuffer.GetData(), chunk, &dwBytesRead)))
            {
                return hr;
            }

            CopyMemory(data.GetData(), localReadBuffer.GetData(), dwBytesRead);
            localReadBuffer.RemoveAll();

            if (dwBytesRead >= ullBytesToRead)
                ullBytesRead = ullBytesToRead;
            else
                ullBytesRead = dwBytesRead;
        }
        else
        {
            //
            // now read
            //
            DWORD dwBytesRead = 0;
            if (FAILED(hr = Extent.Read(data.GetData(), chunk, &dwBytesRead)))
            {
                return hr;
            }

            if (dwBytesRead >= ullBytesToRead)
                ullBytesRead = ullBytesToRead;
            else
                ullBytesRead = dwBytesRead;
        }
    }
    else
    {
        // we round down to the lower sector boundary
        if (chunk <= m_BytesPerSector)
            chunk = m_BytesPerSector;
        else if (chunk > data.GetCount())
            chunk = (static_cast<DWORD>(data.GetCount()) / m_BytesPerSector) * m_BytesPerSector;

        CBinaryBuffer localReadBuffer(true);

        if (chunk > data.GetCount())
        {
            if (!localReadBuffer.CheckCount(chunk))
                return E_OUTOFMEMORY;

            //
            // now read
            //
            DWORD dwBytesRead = 0;
            if (FAILED(hr = Extent.Read(localReadBuffer.GetData(), chunk, &dwBytesRead)))
            {
                return hr;
            }
            MoveMemory(data.GetData(), localReadBuffer.GetData(), data.GetCount());
            localReadBuffer.RemoveAll();
            ullBytesRead = data.GetCount();
        }
        else
        {
            if (!localReadBuffer.CheckCount(chunk))
                return E_OUTOFMEMORY;
            //
            // now read
            //
            DWORD dwBytesRead = 0;
            if (FAILED(hr = Extent.Read(localReadBuffer.GetData(), chunk, &dwBytesRead)))
            {
                return hr;
            }
            if (dwBytesRead >= ullBytesToRead)
                ullBytesRead = ullBytesToRead;
            else
                ullBytesRead = dwBytesRead;

            CopyMemory(data.GetData(), localReadBuffer.GetData(), dwBytesRead);
            localReadBuffer.RemoveAll();
        }
    }
    return S_OK;
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
            log::Error(_L_, hr, L"Failed to read the first 512 bytes!\r\n");

            if (!buffer.SetCount(4096))
                return E_OUTOFMEMORY;

            if (FAILED(hr = m_Extents[0].Read(buffer.GetData(), 4096, &dwBytesRead)))
            {
                log::Error(_L_, hr, L"Failed to read the first 4096 bytes!\r\n");
                return hr;
            }
        }

        return VolumeReader::ParseBootSector(buffer);
    }

    return hr;
}

CompleteVolumeReader::~CompleteVolumeReader() {}
