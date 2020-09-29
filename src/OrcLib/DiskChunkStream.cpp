//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "DiskChunkStream.h"

using namespace Orc;

HRESULT __stdcall DiskChunkStream::Open()
{
    HRESULT hr = E_FAIL;

    // m_diskReader->Open method request a GENERIC_READ access
    if (SUCCEEDED(hr = m_diskReader->Open(FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING)))
    {
        m_isOpen = true;
        hr = S_OK;
    }
    else
    {
        spdlog::error(
            L"DiskChunkStream::Open: Failed to open a disk for read access : {} (code: {:#x})", m_DiskInterface, hr);
    }
    return hr;
}

HRESULT __stdcall DiskChunkStream::IsOpen()
{
    if (m_isOpen == true)
    {
        return S_OK;
    }
    else
    {
        return E_FAIL;
    }
}

HRESULT __stdcall DiskChunkStream::Close()
{
    if (m_isOpen)
    {
        m_diskReader->Close();
    }
    return S_OK;
}

DiskChunkStream::~DiskChunkStream()
{
    Close();
}

HRESULT __stdcall DiskChunkStream::CanRead()
{
    if (m_isOpen == true)
    {
        return S_OK;
    }
    else
    {
        return E_FAIL;
    }
}

HRESULT __stdcall DiskChunkStream::Read(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesRead)
{
    ULONGLONG diskSize = m_diskReader->GetLength();
    ULONG diskSectorSize = m_diskReader->GetLogicalSectorSize();
    HRESULT hr = E_FAIL;
    ULONGLONG bytesToRead = cbBytes;

    if (pcbBytesRead != NULL)
    {
        *pcbBytesRead = 0;
    }

    if (diskSize == 0 || diskSectorSize == 0)
    {
        spdlog::error(L"[DiskChunkStream::Read] Unknown disk size or sector size");
        return E_FAIL;
    }

    if (m_chunkPointer > m_size)
    {
        spdlog::error(L"[DiskChunkStream::Read] Internal chunk pointer is out of bounds\n");
        return hr;
    }

    // Make sure we do not read after the chunk
    if (cbBytes > (m_size - m_chunkPointer))
    {
        bytesToRead = (m_size - m_chunkPointer);
    }

    // Adjust byteToRead to take into account the disk offset aligned with the disk sector size.
    // This aligned disk offset is not necessarily the same as the exact disk offset pointing inside the chunk where we
    // want to read
    bytesToRead += m_deltaFromDiskToChunkPointer;

    // Potentially adjust bytesToRead to a sector size multiple (round up)
    ULONGLONG deltaReadSize = 0;
    if (bytesToRead % diskSectorSize != 0)
    {
        deltaReadSize = (diskSectorSize - (bytesToRead % diskSectorSize));
        bytesToRead = bytesToRead + deltaReadSize;
    }

    // Potentially truncate size to not read beyond end of disk
    if (m_offset + bytesToRead > diskSize)
    {
        bytesToRead = diskSize - m_offset;
    }

    CBinaryBuffer cBuf(true);
    cBuf.SetCount((size_t)bytesToRead);
    cBuf.ZeroMe();
    DWORD numberOfbytesRead = 0;

    if (bytesToRead != 0)
    {
        // Read all data at once. If an error occurs, no data will be available.
        if (FAILED(hr = m_diskReader->Read(cBuf.GetData(), (DWORD)bytesToRead, &numberOfbytesRead)))
        {
            // Free the memory for the buffer, m_cBuf.GetData() will return NULL
            cBuf.RemoveAll();
            spdlog::error(
                L"DiskChunkStream::Read: Failed to read at offset {} ({}) (code: {:#x})",
                m_offset + m_chunkPointer - m_deltaFromDiskToChunkPointer,
                m_DiskInterface,
                hr);
            return hr;
        }
    }

    ULONGLONG numberOfChunkBytesRead = numberOfbytesRead - m_deltaFromDiskToChunkPointer - deltaReadSize;

    // Consistency checks
    if ((numberOfbytesRead < (m_deltaFromDiskToChunkPointer + deltaReadSize)) || (numberOfChunkBytesRead > cbBytes))
    {
        cBuf.RemoveAll();
        spdlog::error(L"DiskChunkStream::Read: Consistency checks done after reading failed");
        return E_FAIL;
    }

    memcpy_s(pBuffer, (size_t)cbBytes, cBuf.GetData() + m_deltaFromDiskToChunkPointer, (size_t)numberOfChunkBytesRead);

    m_chunkPointer += numberOfChunkBytesRead;

    if (pcbBytesRead != NULL)
    {
        *pcbBytesRead = numberOfChunkBytesRead;
    }

    cBuf.RemoveAll();
    return S_OK;
}

HRESULT __stdcall DiskChunkStream::CanWrite()
{
    return E_FAIL;
}

HRESULT __stdcall DiskChunkStream::Write(const PVOID pBuffer, ULONGLONG cbBytes, PULONGLONG pcbBytesWritten)
{
    return E_FAIL;
}

HRESULT __stdcall DiskChunkStream::CanSeek()
{
    if (m_isOpen == true)
    {
        return S_OK;
    }
    else
    {
        return E_FAIL;
    }
}

HRESULT __stdcall DiskChunkStream::SetFilePointer(
    __in LONGLONG DistanceToMove,
    __in DWORD dwMoveMethod,
    __out_opt PULONG64 pCurrPointer)
{
    ULONGLONG diskSize = m_diskReader->GetLength();
    ULONG diskSectorSize = m_diskReader->GetLogicalSectorSize();
    HRESULT hr = E_FAIL;

    if (diskSize == 0 || diskSectorSize == 0)
    {
        spdlog::error(L"[DiskChunkStream::SetFilePointer] Unknown disk size or sector size");
        return E_FAIL;
    }

    // Move the chunk pointer
    ULONG64 movedChunkPointer = 0;

    if (dwMoveMethod == FILE_BEGIN)
    {
        movedChunkPointer = DistanceToMove;
    }
    else if (dwMoveMethod == FILE_CURRENT)
    {
        movedChunkPointer = m_chunkPointer + DistanceToMove;
    }
    else if (dwMoveMethod == FILE_END)
    {
        movedChunkPointer = m_size - DistanceToMove;
    }
    else
    {
        return E_FAIL;
    }

    // Make sure that the chunk pointer is still within the chunk boundaries
    if (movedChunkPointer > m_size)
    {
        return E_FAIL;
    }

    // Determine the disk offset of the chunk pointer
    LARGE_INTEGER diskOffset;
    diskOffset.QuadPart = m_offset + movedChunkPointer;

    // Make potential adjusment to the disk offset to be aligned on a sector boundary (round down diskOffset to a sector
    // size multiple)
    ULONGLONG deltaFromDiskToChunkPointer = (diskOffset.QuadPart % diskSectorSize);
    diskOffset.QuadPart = diskOffset.QuadPart - deltaFromDiskToChunkPointer;

    if (diskOffset.QuadPart < 0 || (ULONGLONG)diskOffset.QuadPart >= diskSize)
    {
        spdlog::error(
            L"[DiskChunkStream::SetFilePointer] Offset is beyond disk boundaries. (offset = %lld, disk size = "
            L"%llu)",
            diskOffset.QuadPart,
            diskSize);
        return E_FAIL;
    }

    // Seek to the raw disk offset
    if (FAILED(hr = m_diskReader->Seek(diskOffset, NULL, FILE_BEGIN)))
    {
        spdlog::error(
            L"DiskChunkStream::SetFilePointer: Failed to seek at offset {} ({}) (code: {:#x})",
            diskOffset.QuadPart,
            m_DiskInterface,
            hr);
        return hr;
    }

    m_chunkPointer = movedChunkPointer;
    m_deltaFromDiskToChunkPointer = deltaFromDiskToChunkPointer;

    if (pCurrPointer != NULL)
    {
        *pCurrPointer = m_chunkPointer;
    }

    return hr;
}

ULONG64 __stdcall DiskChunkStream::GetSize()
{
    return m_size;
}

HRESULT __stdcall DiskChunkStream::SetSize(ULONG64)
{
    return E_FAIL;
}

std::wstring DiskChunkStream::getSampleName()
{
    std::wstring sanitizedDiskName(m_DiskName);
    std::replace(sanitizedDiskName.begin(), sanitizedDiskName.end(), '\\', '_');
    std::wstring tmpDescription(m_description);
    std::replace(tmpDescription.begin(), tmpDescription.end(), ' ', '-');
    return sanitizedDiskName + L"_off_" + std::to_wstring(m_offset) + L"_len_" + std::to_wstring(m_size) + L"_"
        + tmpDescription + L".bin";
}
