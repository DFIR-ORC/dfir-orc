//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "MemoryStream.h"

#include "Temporary.h"
#include "BinaryBuffer.h"

#include "OrcException.h"

#include "SystemDetails.h"

using namespace Orc;
using namespace std::string_view_literals;

// The minimum number of bytes to allocate for a memory stream buffer
static const size_t MEMORY_STREAM_MIN_ALLOC = 4096;

// The maximum byte size after which the buffer STOP growing exponentialy
static const size_t MEMORY_STREAM_EXPONENTIAL_THRESHOLD = ((1024 * 1024 * 32) + 1);

// The amount to grow the buffer after exponential growth is stopped.
static const size_t MEMORY_STREAM_THRESHOLD_INCREMENT = (1024 * 1024 * 16);

static const size_t MEMORY_STREAM_RESERVE_MAX = (1024 * 1024 * 200);

STDMETHODIMP Orc::MemoryStream::Clone(std::shared_ptr<ByteStream>& clone)
{
    auto new_stream = std::make_shared<MemoryStream>();

    if (auto hr = new_stream->Duplicate(*this); FAILED(hr))
        return hr;

    ULONG64 ullCurPos = 0LLU;
    if (auto hr = SetFilePointer(0LL, FILE_CURRENT, &ullCurPos); FAILED(hr))
        return hr;

    ULONG64 ullDupCurPos = 0LLU;
    if (auto hr = new_stream->SetFilePointer(ullCurPos, FILE_BEGIN, &ullDupCurPos); FAILED(hr))
        return hr;

    clone = new_stream;
    return S_OK;
}

HRESULT MemoryStream::Close()
{
    return S_OK;
}

HRESULT MemoryStream::Duplicate(const MemoryStream& other)
{
    if (other.m_bReadOnly)
    {
        m_pBuffer = other.m_pBuffer;

        m_bReadOnly = other.m_bReadOnly;
        m_cbBuffer = other.m_cbBuffer;
        m_cbReservedBytes = other.m_cbReservedBytes;
        m_cbBufferCommitSize = other.m_cbBufferCommitSize;
        m_dwCurrFilePointer = 0LL;
    }
    else
    {
        m_pBuffer = (LPBYTE)VirtualAlloc(NULL, other.m_cbReservedBytes, MEM_RESERVE, PAGE_READWRITE);
        if (m_pBuffer == NULL)
            return E_OUTOFMEMORY;
        m_pBuffer = (LPBYTE)VirtualAlloc(NULL, other.m_cbBufferCommitSize, MEM_COMMIT, PAGE_READWRITE);
        if (m_pBuffer == NULL)
            return E_OUTOFMEMORY;

        CopyMemory(m_pBuffer, other.m_pBuffer, other.m_cbBuffer);

        m_bReadOnly = other.m_bReadOnly;
        m_cbBuffer = other.m_cbBuffer;
        m_cbReservedBytes = other.m_cbReservedBytes;
        m_cbBufferCommitSize = other.m_cbBufferCommitSize;
        m_dwCurrFilePointer = 0LL;
    }
    return S_OK;
}

/*
MemoryStream::OpenForReadWrite

Creates an empty stream which can be written to. All writes to the  stream will
be kept in a growable memory buffer
*/
HRESULT MemoryStream::OpenForReadWrite(DWORD dwSize, DWORD dwReservedBytes)
{
    HRESULT hr = E_FAIL;
    Close();

    if (FAILED(hr = SetBufferSize(dwSize, dwReservedBytes)))
        return hr;

    m_bReadOnly = false;
    return S_OK;
}

/*
MemoryStream::OpenForReadOnly

Creates a read-only stream from the passed in buffer
*/
HRESULT MemoryStream::OpenForReadOnly(__in PVOID pBuffer, __in size_t cbBuffer)
{
    Close();

    m_pBuffer = (PBYTE)pBuffer;
    m_cbBuffer = cbBuffer;
    m_bReadOnly = true;

    return S_OK;
}

/*
MemoryStream::Read

Reads data from the stream

Parameters:
pReadBuffer     -   Pointer to buffer which receives the data
cbBytesToRead   -   Number of bytes to read from stream
pcbBytesRead    -   Recieves the number of bytes copied to pReadBuffer
operation
*/
HRESULT MemoryStream::Read_(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesRead)
{
    size_t dwBytesRead = 0;

    if (pReadBuffer == NULL)
        return E_INVALIDARG;

    if (m_pBuffer)
    {
        dwBytesRead = std::min(m_cbBuffer - m_dwCurrFilePointer, static_cast<size_t>(cbBytes));
        CopyMemory(pReadBuffer, &m_pBuffer[m_dwCurrFilePointer], dwBytesRead);
        m_dwCurrFilePointer += dwBytesRead;
    }
    else
    {
        dwBytesRead = 0;
    }

    if (pcbBytesRead)
    {
        *pcbBytesRead = dwBytesRead;
    }
    return S_OK;
}

HRESULT MemoryStream::CommitBuffer(size_t dwPosition, size_t dwCommitSize)
{
    HRESULT hr = E_FAIL;

    if ((dwPosition + dwCommitSize) > m_cbReservedBytes)
    {
        if (FAILED(hr = SetBufferSize(dwPosition + dwCommitSize, m_cbReservedBytes)))
            return hr;
    }

    LPBYTE pCurPos = m_pBuffer + dwPosition;

    pCurPos = static_cast<PBYTE>(VirtualAlloc(pCurPos, dwCommitSize, MEM_COMMIT, PAGE_READWRITE));
    if (pCurPos == NULL)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT MemoryStream::SetBufferSize(size_t dwCommitSize, size_t dwReserveSize)
{
    HRESULT hr = E_FAIL;
    PBYTE pNewBuffer = NULL;

    if (dwCommitSize > dwReserveSize)
        dwReserveSize = dwCommitSize;

    if (m_cbReservedBytes != dwReserveSize)
    {
        if (m_pBuffer == nullptr)
        {
            m_pBuffer = (PBYTE)VirtualAlloc(
                NULL, std::min(MEMORY_STREAM_RESERVE_MAX, dwReserveSize), MEM_RESERVE, PAGE_READWRITE);
            if (!m_pBuffer)
            {
                Log::Debug("Could not reserve {} bytes", dwReserveSize);
            }
            else
                m_cbReservedBytes = dwReserveSize;
        }
    }

    if (dwCommitSize > 0)
    {
        // first try to stay where we are...
        pNewBuffer = (PBYTE)VirtualAlloc(
            m_pBuffer, dwCommitSize, m_pBuffer == NULL ? (MEM_COMMIT | MEM_RESERVE) : MEM_COMMIT, PAGE_READWRITE);

        if (!pNewBuffer)
        {
            // if that fails, ask any location
            pNewBuffer = (PBYTE)VirtualAlloc(NULL, dwCommitSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        }

        if (!pNewBuffer)
        {
            Log::Debug("Could not allocate {} bytes", dwCommitSize);
            return E_OUTOFMEMORY;
        }
    }

    if (dwCommitSize < m_cbBufferCommitSize)
    {
        DWORD dwPageSize = 0L;
        if (FAILED(hr = SystemDetails::GetPageSize(dwPageSize)))
        {
            Log::Error(L"Failed to decommit pages: could not retrieve page size [{}]", SystemError(hr));
        }
        else
        {
            size_t dwPagesToUnCommit = (m_cbBufferCommitSize - dwCommitSize) / dwPageSize;

            if (dwPagesToUnCommit > 0)
            {
                LPBYTE pFirstPageToDecommit = m_pBuffer + (((dwCommitSize / dwPageSize) + 1) * dwPageSize);
                if (!VirtualFree(pFirstPageToDecommit, dwPagesToUnCommit * dwPageSize, MEM_DECOMMIT))
                {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    Log::Error("Failed to decommit pages [{}]", SystemError(hr));
                }
            }
        }
    }

    if (m_pBuffer != pNewBuffer && m_pBuffer != nullptr)
    {
        CopyMemory(pNewBuffer, m_pBuffer, std::min(m_cbBuffer, dwCommitSize));
        VirtualFree(m_pBuffer, 0L, MEM_RELEASE);
    }
    m_pBuffer = pNewBuffer;
    m_cbBufferCommitSize = dwCommitSize;
    return S_OK;
}

/*

MemoryStream::Write

Writes data to the memory stream. The buffer grows to accomidate data
written to the stream.

Parameters:
pWriteBuffer    -   Pointer to buffer to write from
cbBytesToWrite  -   Number of bytes to write to the stream
pcbBytesWritten -   Recieves the number of written to the stream
*/
HRESULT MemoryStream::Write_(
    __in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
    __in ULONGLONG cbBytesToWrite,
    __out_opt PULONGLONG pcbBytesWritten)
{
    HRESULT hr = E_FAIL;
    PBYTE pNewBuffer = NULL;
    size_t dwAllocSize = 0;

    if (pWriteBuffer == NULL)
        return E_INVALIDARG;

    if (m_bReadOnly)
    {
        Log::Error("Invalid write to read-only memory stream");
        return E_ACCESSDENIED;
    }

    if (m_dwCurrFilePointer + cbBytesToWrite < m_dwCurrFilePointer)
    {
        hr = HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        Log::Error("File pointer overflowed");
        return hr;
    }

    // Check whether we need to expand the buffer
    if (m_pBuffer == NULL || m_cbBufferCommitSize < cbBytesToWrite + m_dwCurrFilePointer)
    {
        // Expand the buffer
        pNewBuffer = NULL;
        dwAllocSize = std::max(MEMORY_STREAM_MIN_ALLOC, m_cbBufferCommitSize);

        if (dwAllocSize < MEMORY_STREAM_EXPONENTIAL_THRESHOLD)
        {
            // Exponentially increase the size of the buffer by rounding up the
            // number of needed bytes to a power of 2
            while (dwAllocSize < cbBytesToWrite + m_dwCurrFilePointer && dwAllocSize < dwAllocSize * 2)
            {
                dwAllocSize *= 2;
            }

            if (dwAllocSize < cbBytesToWrite + m_dwCurrFilePointer)
            {
                Log::Error("Alloc size overflowed while rounding up");
                return E_OUTOFMEMORY;
            }
        }
        else
        {
            // Threshold was reached, start growing the buffer linearly.
            dwAllocSize = static_cast<size_t>(cbBytesToWrite) + m_dwCurrFilePointer + MEMORY_STREAM_THRESHOLD_INCREMENT;
        }

        if (FAILED(hr = SetBufferSize(dwAllocSize, m_cbReservedBytes)))
            return hr;
    }

    // Copy data into the stream buffer
    CopyMemory(m_pBuffer + m_dwCurrFilePointer, pWriteBuffer, (size_t)cbBytesToWrite);

    m_dwCurrFilePointer += static_cast<size_t>(cbBytesToWrite);
    m_cbBuffer = std::max(m_dwCurrFilePointer, m_cbBuffer);

    if (pcbBytesWritten)
    {
        *pcbBytesWritten = cbBytesToWrite;
    }
    return S_OK;
}

/*
MemoryStream::SetFilePointer

Sets the stream's current pointer. If the resultant pointer is greater
than the number of valid bytes in the buffer, the resultant is clipped
to the latter value

Parameters:
See SetFilePointer() doc for the descriptions of all parameters
*/
HRESULT
MemoryStream::SetFilePointer(__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer)
{
    HRESULT hr = E_FAIL;
    DWORD dwNewFilePointer = 0;

    if (DistanceToMove > MAXDWORD)
        return E_INVALIDARG;
    LARGE_INTEGER liDistanceToMove;
    liDistanceToMove.QuadPart = DistanceToMove;

    switch (dwMoveMethod)
    {
        case SEEK_CUR:
            dwNewFilePointer = LONG(m_dwCurrFilePointer) + liDistanceToMove.LowPart;
            break;
        case SEEK_END:
            dwNewFilePointer = (LONG)m_cbBuffer + liDistanceToMove.LowPart;
            break;

        case SEEK_SET:
            dwNewFilePointer = liDistanceToMove.LowPart;
            break;

        default:
            return E_INVALIDARG;
    }

    // Clip the pointer to [0, cbBuffer]
    if (dwNewFilePointer > m_cbBuffer)
    {
        if (FAILED(hr = SetBufferSize(dwNewFilePointer, m_cbReservedBytes)))
            return hr;
    }
    m_dwCurrFilePointer = dwNewFilePointer;

    if (pCurrPointer)
    {
        *pCurrPointer = m_dwCurrFilePointer;
    }
    return S_OK;
}

/*
MemoryStream::GetSize

Returns the number of valid bytes in the stream buffer
*/
ULONG64 MemoryStream::GetSize()
{
    return m_cbBuffer;
}

HRESULT MemoryStream::SetSize(ULONG64 ullNewSize)
{
    if (ullNewSize > MAXDWORD)
        return E_INVALIDARG;

    // Truncate the buffer to have the same behavior than FileStream with
    // SetEndOfFile
    if (ullNewSize < m_cbBuffer)
    {
        m_cbBuffer = ullNewSize;
        return S_OK;
    }

    return SetBufferSize(0LL, (size_t)ullNewSize);
}

MemoryStream::MemoryStream()
    : ByteStream()
{
    m_cbBuffer = 0;
    m_cbBufferCommitSize = 0;
    m_cbReservedBytes = 0;
    m_pBuffer = NULL;
    m_bReadOnly = FALSE;
    m_dwCurrFilePointer = 0;
}

CBinaryBuffer MemoryStream::GetBuffer()
{
    CBinaryBuffer retval;

    if (auto hr = retval.SetData(m_pBuffer, m_cbBuffer); FAILED(hr))
        throw Exception(Severity::Fatal, hr, L"Failed to copy buffer of {} bytes"sv, m_cbBuffer);

    return retval;
}

const CBinaryBuffer MemoryStream::GetConstBuffer() const
{
    CBinaryBuffer retval(m_pBuffer, m_cbBuffer);
    return std::move(retval);
}

void Orc::MemoryStream::GrabBuffer(CBinaryBuffer& buffer)
{
    buffer.RemoveAll();
    std::swap(m_pBuffer, buffer.m_pData);
    std::swap(m_cbBuffer, buffer.m_size);
    buffer.m_bVirtualAlloc = true;
    buffer.m_bOwnMemory = true;
    buffer.m_bJunk = false;
}

MemoryStream::~MemoryStream()
{
    if (!m_bReadOnly && m_pBuffer)
    {
        VirtualFree(m_pBuffer, 0, MEM_RELEASE);
        m_pBuffer = NULL;
        m_cbBuffer = 0;
        m_cbBufferCommitSize = 0;
        m_dwCurrFilePointer = 0;
    }
}
