//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "MultiMemoryStream.h"

#include "Temporary.h"

using namespace Orc;

HRESULT MultiMemoryStream::Close()
{
    if (!m_bReadOnly)
    {
    }
    return S_OK;
}

HRESULT MultiMemoryStream::OpenForReadWrite()
{
    Close();

    m_bReadOnly = FALSE;
    return S_OK;
}

/*
    MultiMemoryStream::OpenForReadOnly

    Creates a read-only stream from the passed in buffer

    Parameters:
        pBuffer     -   Pointer to buffer which holds the stream's contents
        cbBuffer    -   Number of bytes in pBuffer
*/
HRESULT MultiMemoryStream::OpenForReadOnly(__in PVOID pBuffer, __in DWORD cbBuffer)
{
    DBG_UNREFERENCED_PARAMETER(pBuffer);
    DBG_UNREFERENCED_PARAMETER(cbBuffer);

    Close();

    m_bReadOnly = TRUE;

    return S_OK;
}

/*
    MultiMemoryStream::Read

    Reads data from the stream

    Parameters:
        pReadBuffer     -   Pointer to buffer which receives the data
        cbBytesToRead   -   Number of bytes to read from stream
        pcbBytesRead    -   Recieves the number of bytes copied to pReadBuffer
*/
HRESULT MultiMemoryStream::Read_(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesRead)
{
    DBG_UNREFERENCED_PARAMETER(pReadBuffer);
    DBG_UNREFERENCED_PARAMETER(cbBytes);
    DBG_UNREFERENCED_PARAMETER(pcbBytesRead);
    if (pReadBuffer == NULL)
        return E_INVALIDARG;
    return S_OK;
}

/*
    CMemoryByteStream::Write

    Writes data to the memory stream. The buffer grows to accomidate data
    written to the stream. The size keeps doubling until the buffer is bigger
    than MEMORY_STREAM_EXPONENTIAL_THRESHOLD, after which it grows in
    MEMORY_STREAM_THRESHOLD_INCREMENT increments

    Parameters:
        pWriteBuffer    -   Pointer to buffer to write from
        cbBytesToWrite  -   Number of bytes to write to the stream
        pcbBytesWritten -   Recieves the number of written to the stream
*/
HRESULT MultiMemoryStream::Write_(
    __in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
    __in ULONGLONG,
    __out_opt PULONGLONG pcbBytesWritten)
{
    DBG_UNREFERENCED_PARAMETER(pcbBytesWritten);
    if (pWriteBuffer == NULL)
        return E_INVALIDARG;

    if (m_bReadOnly)
    {
        Log::Error("Invalid write to read-only memory stream");
        return E_ACCESSDENIED;
    }

    return S_OK;
}

/*
    MultiMemoryStream::GetSize

    Returns the number of valid bytes in the stream buffer
*/
ULONG64 MultiMemoryStream::GetSize()
{
    return 0L;
}

STDMETHODIMP MultiMemoryStream::SetSize(ULONG64)
{
    return E_NOTIMPL;
}

/*
    MultiMemoryStream::SetFilePointer

    Sets the stream's current pointer. If the resultant pointer is greater
    than the number of valid bytes in the buffer, the resultant is clipped
    to the latter value

    Parameters:
        See SetFilePointer() doc for the descriptions of all parameters
*/
HRESULT MultiMemoryStream::SetFilePointer(__in LONGLONG, __in DWORD dwMoveMethod, __out_opt PULONG64)
{

    switch (dwMoveMethod)
    {
        case SEEK_CUR:

            // dwNewFilePointer = LONG(m_dwCurrFilePointer) + DistanceToMove;
            break;

        case SEEK_END:

            // dwNewFilePointer = (LONG)m_cbBuffer + DistanceToMove;
            break;

        case SEEK_SET:

            // dwNewFilePointer = DistanceToMove;
            break;

        default:
            return E_INVALIDARG;
    }

    // Clip the pointer to [0, cbBuffer]
    // dwNewFilePointer = min(m_cbBuffer, dwNewFilePointer);

    return S_OK;
}

MultiMemoryStream::MultiMemoryStream()
    : ByteStream()
{
    m_bReadOnly = FALSE;
}

MultiMemoryStream::~MultiMemoryStream()
{
    Close();
}
